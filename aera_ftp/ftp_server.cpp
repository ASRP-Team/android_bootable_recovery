/* AERA recovery FTP server.
 *
 * A small, dependency-light FTP (RFC 959) daemon for legacy clients.
 * It runs as root inside recovery, authenticates against the same credential
 * file used by the SSH server (/etc/aera_ssh_password) and exposes the whole
 * recovery filesystem.  Only plain FTP is implemented - there is no TLS.
 */
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <openssl/sha.h>

namespace {

constexpr const char* kPasswordFile = "/etc/aera_ssh_password";
constexpr int kDefaultPort = 2121;
constexpr int kMaxLine = 8192;
constexpr int kAuthAttempts = 3;

struct Session {
  int ctrl = -1;
  sockaddr_in peer{};
  std::string cwd = "/";
  std::string input;
  std::string rename_from;
  int pasv_fd = -1;
  bool active_set = false;
  sockaddr_in active{};
  long long rest = 0;
  bool user_seen = false;
  bool authed = false;
  int failures = 0;
};

bool SendAll(int fd, const void* data, size_t len) {
  const char* p = static_cast<const char*>(data);
  size_t done = 0;
  while (done < len) {
    ssize_t n = send(fd, p + done, len - done, MSG_NOSIGNAL);
    if (n > 0) {
      done += static_cast<size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

void SendLine(int fd, const std::string& line) {
  std::string out = line;
  out += "\r\n";
  SendAll(fd, out.data(), out.size());
}

void Reply(int fd, const char* code, const std::string& text) {
  SendLine(fd, std::string(code) + " " + text);
}

int ReadLine(int fd, std::string& buffer, std::string* line) {
  for (;;) {
    const size_t pos = buffer.find('\n');
    if (pos != std::string::npos) {
      std::string value = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      if (!value.empty() && value.back() == '\r') value.pop_back();
      *line = std::move(value);
      return 1;
    }
    char chunk[1024];
    const ssize_t n = read(fd, chunk, sizeof(chunk));
    if (n > 0) {
      buffer.append(chunk, static_cast<size_t>(n));
      if (buffer.size() > kMaxLine) return -1;
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    return static_cast<int>(n);
  }
}

std::string Normalize(const std::string& base, const std::string& arg) {
  const std::string joined =
      arg.empty() ? base : (arg.front() == '/' ? arg : base + "/" + arg);
  std::vector<std::string> parts;
  size_t i = 0;
  while (i < joined.size()) {
    while (i < joined.size() && joined[i] == '/') i++;
    size_t j = i;
    while (j < joined.size() && joined[j] != '/') j++;
    const std::string part = joined.substr(i, j - i);
    i = j;
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      if (!parts.empty()) parts.pop_back();
      continue;
    }
    parts.push_back(part);
  }
  std::string out;
  for (const auto& part : parts) {
    out += "/";
    out += part;
  }
  return out.empty() ? "/" : out;
}

std::string Parent(const std::string& path) {
  if (path == "/" || path.empty()) return "/";
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

std::string BaseName(const std::string& path) {
  if (path == "/") return "/";
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool HexDecode(const std::string& hex, std::vector<unsigned char>* out) {
  if (hex.size() % 2 != 0) return false;
  out->clear();
  out->reserve(hex.size() / 2);
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int high = nibble(hex[i]);
    const int low = nibble(hex[i + 1]);
    if (high < 0 || low < 0) return false;
    out->push_back(static_cast<unsigned char>((high << 4) | low));
  }
  return true;
}

bool ConstantTimeEqual(const unsigned char* a, const unsigned char* b,
                       size_t len) {
  unsigned char diff = 0;
  for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

bool VerifyPassword(const std::string& password) {
  int fd = open(kPasswordFile, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  char buf[512] = {};
  const ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return false;

  const std::string content(buf, static_cast<size_t>(n));
  const size_t first = content.find(':');
  if (first == std::string::npos || content.compare(0, first, "sha256") != 0)
    return false;
  const size_t second = content.find(':', first + 1);
  const size_t end = content.find_first_of("\r\n", second == std::string::npos
                                                     ? 0
                                                     : second + 1);
  if (second == std::string::npos) return false;

  const std::string salt_hex = content.substr(first + 1, second - first - 1);
  const std::string hash_hex =
      content.substr(second + 1, (end == std::string::npos ? content.size() : end) -
                                     second - 1);
  std::vector<unsigned char> salt;
  std::vector<unsigned char> expected;
  if (!HexDecode(salt_hex, &salt) || !HexDecode(hash_hex, &expected)) return false;
  if (expected.size() != SHA256_DIGEST_LENGTH) return false;

  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  if (!salt.empty()) SHA256_Update(&ctx, salt.data(), salt.size());
  SHA256_Update(&ctx, password.data(), password.size());
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256_Final(digest, &ctx);
  return ConstantTimeEqual(digest, expected.data(), SHA256_DIGEST_LENGTH);
}

std::string PermissionString(mode_t mode) {
  std::string out = "----------";
  out[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : '-';
  const char flags[] = {'r', 'w', 'x'};
  for (int i = 0; i < 9; i++) {
    if (mode & (1 << (8 - i))) out[static_cast<size_t>(i + 1)] = flags[i % 3];
  }
  return out;
}

std::string TimeString(time_t when) {
  char out[32] = {};
  struct tm tm_value;
  localtime_r(&when, &tm_value);
  const time_t now = time(nullptr);
  if (now - when > 180L * 24 * 60 * 60) {
    strftime(out, sizeof(out), "%b %e  %Y", &tm_value);
  } else {
    strftime(out, sizeof(out), "%b %e %H:%M", &tm_value);
  }
  return out;
}

std::string ListLine(const std::string& name, const struct stat& info) {
  char size[32] = {};
  std::snprintf(size, sizeof(size), "%lld",
                static_cast<long long>(info.st_size));
  std::string line = PermissionString(info.st_mode);
  line += " 1 root root ";
  line += size;
  line += " ";
  line += TimeString(info.st_mtime);
  line += " ";
  line += name;
  return line;
}

int OpenData(Session& session) {
  if (session.pasv_fd >= 0) {
    struct pollfd pfd = {session.pasv_fd, POLLIN, 0};
    if (poll(&pfd, 1, 30000) <= 0) {
      close(session.pasv_fd);
      session.pasv_fd = -1;
      return -1;
    }
    sockaddr_in from{};
    socklen_t len = sizeof(from);
    const int fd = accept(session.pasv_fd, reinterpret_cast<sockaddr*>(&from), &len);
    close(session.pasv_fd);
    session.pasv_fd = -1;
    if (fd < 0) return -1;
    if (from.sin_addr.s_addr != session.peer.sin_addr.s_addr) {
      close(fd);
      return -1;
    }
    return fd;
  }
  if (session.active_set) {
    session.active_set = false;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, reinterpret_cast<sockaddr*>(&session.active),
                sizeof(session.active)) != 0) {
      close(fd);
      return -1;
    }
    return fd;
  }
  return -1;
}

bool StartPassive(Session& session) {
  if (session.pasv_fd >= 0) {
    close(session.pasv_fd);
    session.pasv_fd = -1;
  }
  const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = 0;
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(fd, 1) != 0) {
    close(fd);
    return false;
  }
  session.pasv_fd = fd;
  return true;
}

bool SendListing(Session& session, const std::string& path, bool names_only) {
  Reply(session.ctrl, "150", "Opening data connection.");
  const int data = OpenData(session);
  if (data < 0) {
    Reply(session.ctrl, "425", "Could not open data connection.");
    return false;
  }

  bool ok = true;
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) {
    Reply(session.ctrl, "550", "No such file or directory.");
    close(data);
    return false;
  }

  if (!S_ISDIR(info.st_mode)) {
    const std::string line = names_only ? BaseName(path)
                                        : ListLine(BaseName(path), info);
    SendLine(data, line);
    close(data);
    Reply(session.ctrl, "226", "Transfer complete.");
    return true;
  }

  DIR* dir = opendir(path.c_str());
  if (!dir) {
    close(data);
    Reply(session.ctrl, "550", "Could not read directory.");
    return false;
  }

  std::vector<std::string> names;
  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    names.push_back(name);
  }
  closedir(dir);
  std::sort(names.begin(), names.end());

  for (const auto& name : names) {
    const std::string full = path == "/" ? "/" + name : path + "/" + name;
    struct stat child{};
    if (lstat(full.c_str(), &child) != 0) continue;
    const std::string line = names_only ? name : ListLine(name, child);
    if (!SendAll(data, line.data(), line.size()) ||
        !SendAll(data, "\r\n", 2)) {
      ok = false;
      break;
    }
  }
  close(data);
  Reply(session.ctrl, ok ? "226" : "426",
        ok ? "Transfer complete." : "Connection closed; transfer aborted.");
  return ok;
}

bool SendFile(Session& session, const std::string& path) {
  const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (file < 0) {
    Reply(session.ctrl, "550", "Could not open file.");
    return false;
  }
  struct stat info{};
  if (fstat(file, &info) != 0 || S_ISDIR(info.st_mode)) {
    close(file);
    Reply(session.ctrl, "550", "Not a regular file.");
    return false;
  }

  const int data = OpenData(session);
  if (data < 0) {
    close(file);
    Reply(session.ctrl, "425", "Could not open data connection.");
    return false;
  }

  if (session.rest > 0) {
    if (lseek(file, static_cast<off_t>(session.rest), SEEK_SET) < 0) {
      session.rest = 0;
      close(file);
      close(data);
      Reply(session.ctrl, "550", "Invalid restart offset.");
      return false;
    }
  }
  session.rest = 0;

  Reply(session.ctrl, "150", "Opening data connection.");
  bool ok = true;
  char buffer[32768];
  for (;;) {
    const ssize_t n = read(file, buffer, sizeof(buffer));
    if (n > 0) {
      if (!SendAll(data, buffer, static_cast<size_t>(n))) {
        ok = false;
        break;
      }
    } else if (n == 0) {
      break;
    } else if (errno != EINTR) {
      ok = false;
      break;
    }
  }
  close(file);
  close(data);
  Reply(session.ctrl, ok ? "226" : "426",
        ok ? "Transfer complete." : "Connection closed; transfer aborted.");
  return ok;
}

bool ReceiveFile(Session& session, const std::string& path, bool append) {
  int flags = O_WRONLY | O_CREAT | O_CLOEXEC;
  flags |= append ? O_APPEND : O_TRUNC;
  const int file = open(path.c_str(), flags, 0644);
  if (file < 0) {
    Reply(session.ctrl, "550", "Could not create file.");
    return false;
  }

  const int data = OpenData(session);
  if (data < 0) {
    close(file);
    Reply(session.ctrl, "425", "Could not open data connection.");
    return false;
  }

  Reply(session.ctrl, "150", "Ok to send data.");
  bool ok = true;
  char buffer[32768];
  for (;;) {
    const ssize_t n = read(data, buffer, sizeof(buffer));
    if (n > 0) {
      size_t done = 0;
      while (done < static_cast<size_t>(n)) {
        const ssize_t w = write(file, buffer + done, static_cast<size_t>(n) - done);
        if (w > 0) {
          done += static_cast<size_t>(w);
        } else if (w < 0 && errno == EINTR) {
          continue;
        } else {
          ok = false;
          break;
        }
      }
      if (!ok) break;
    } else if (n == 0) {
      break;
    } else if (errno != EINTR) {
      ok = false;
      break;
    }
  }
  close(file);
  close(data);
  Reply(session.ctrl, ok ? "226" : "426",
        ok ? "Transfer complete." : "Connection closed; transfer aborted.");
  return ok;
}

std::string CommandArgument(const std::string& line, size_t* verb_len) {
  size_t space = line.find(' ');
  if (space == std::string::npos) {
    *verb_len = line.size();
    return {};
  }
  *verb_len = space;
  size_t start = space;
  while (start < line.size() && line[start] == ' ') start++;
  return line.substr(start);
}

void HandlePasv(Session& session) {
  if (!StartPassive(session)) {
    Reply(session.ctrl, "425", "Could not enter passive mode.");
    return;
  }
  sockaddr_in local{};
  socklen_t len = sizeof(local);
  if (getsockname(session.ctrl, reinterpret_cast<sockaddr*>(&local), &len) != 0) {
    Reply(session.ctrl, "425", "Could not determine local address.");
    return;
  }
  sockaddr_in bound{};
  len = sizeof(bound);
  if (getsockname(session.pasv_fd, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
    Reply(session.ctrl, "425", "Could not determine passive port.");
    return;
  }
  const uint32_t ip = ntohl(local.sin_addr.s_addr);
  char reply[96] = {};
  std::snprintf(reply, sizeof(reply),
                "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).",
                (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff,
                ip & 0xff, ntohs(bound.sin_port) >> 8, ntohs(bound.sin_port) & 0xff);
  SendLine(session.ctrl, reply);
}

void HandleEpsv(Session& session) {
  if (!StartPassive(session)) {
    Reply(session.ctrl, "425", "Could not enter extended passive mode.");
    return;
  }
  sockaddr_in bound{};
  socklen_t len = sizeof(bound);
  if (getsockname(session.pasv_fd, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
    Reply(session.ctrl, "425", "Could not determine passive port.");
    return;
  }
  char reply[64] = {};
  std::snprintf(reply, sizeof(reply), "229 Entering Extended Passive Mode (|||%u|)",
                ntohs(bound.sin_port));
  SendLine(session.ctrl, reply);
}

void HandlePort(Session& session, const std::string& arg) {
  unsigned int h1 = 0, h2 = 0, h3 = 0, h4 = 0, p1 = 0, p2 = 0;
  if (std::sscanf(arg.c_str(), "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1,
                  &p2) != 6 ||
      h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 || p1 > 255 || p2 > 255) {
    Reply(session.ctrl, "501", "Invalid PORT syntax.");
    return;
  }
  uint32_t address = htonl((static_cast<uint32_t>(h1) << 24) |
                           (static_cast<uint32_t>(h2) << 16) |
                           (static_cast<uint32_t>(h3) << 8) |
                           static_cast<uint32_t>(h4));
  if (address != session.peer.sin_addr.s_addr) {
    Reply(session.ctrl, "501", "Active mode is limited to the control peer.");
    return;
  }
  if (session.pasv_fd >= 0) {
    close(session.pasv_fd);
    session.pasv_fd = -1;
  }
  session.active.sin_family = AF_INET;
  session.active.sin_addr.s_addr = address;
  session.active.sin_port = htons(static_cast<uint16_t>((p1 << 8) | p2));
  session.active_set = true;
  Reply(session.ctrl, "200", "PORT command successful.");
}

bool ParseUserCommand(const std::string& line, std::string* verb,
                      std::string* arg) {
  if (line.empty()) return false;
  size_t verb_len = 0;
  *arg = CommandArgument(line, &verb_len);
  *verb = line.substr(0, verb_len);
  for (auto& c : *verb) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return !verb->empty();
}

void HandleSession(int ctrl, const sockaddr_in& peer) {
  struct timeval receive_timeout = {600, 0};
  struct timeval send_timeout = {60, 0};
  setsockopt(ctrl, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
             sizeof(receive_timeout));
  setsockopt(ctrl, SOL_SOCKET, SO_SNDTIMEO, &send_timeout,
             sizeof(send_timeout));

  Session session;
  session.ctrl = ctrl;
  session.peer = peer;
  Reply(ctrl, "220", "AERA FTP server ready.");

  std::string line;
  while (!line.empty() || ReadLine(ctrl, session.input, &line) > 0) {
    std::string verb;
    std::string arg;
    if (!ParseUserCommand(line, &verb, &arg)) continue;

    if (!session.authed && verb != "USER" && verb != "PASS" && verb != "QUIT" &&
        verb != "NOOP" && verb != "FEAT" && verb != "SYST" && verb != "OPTS") {
      Reply(ctrl, "530", "Please log in with USER and PASS.");
      line.clear();
      continue;
    }

    if (verb == "USER") {
      if (arg == "root") {
        session.user_seen = true;
        Reply(ctrl, "331", "Please specify the password.");
      } else {
        session.user_seen = false;
        Reply(ctrl, "530", "Only the root account is available.");
      }
    } else if (verb == "PASS") {
      if (!session.user_seen) {
        Reply(ctrl, "503", "Send USER first.");
      } else if (VerifyPassword(arg)) {
        session.authed = true;
        Reply(ctrl, "230", "Login successful.");
      } else {
        session.failures++;
        if (session.failures >= kAuthAttempts) {
          Reply(ctrl, "421", "Too many failed login attempts.");
          break;
        }
        Reply(ctrl, "530", "Login incorrect.");
      }
    } else if (verb == "QUIT") {
      Reply(ctrl, "221", "Goodbye.");
      break;
    } else if (verb == "NOOP") {
      Reply(ctrl, "200", "NOOP ok.");
    } else if (verb == "SYST") {
      Reply(ctrl, "215", "UNIX Type: L8");
    } else if (verb == "FEAT") {
      SendLine(ctrl, "211-Features:");
      SendLine(ctrl, " UTF8");
      SendLine(ctrl, " EPSV");
      SendLine(ctrl, " PASV");
      SendLine(ctrl, " SIZE");
      SendLine(ctrl, " MDTM");
      SendLine(ctrl, " REST STREAM");
      SendLine(ctrl, "211 End");
    } else if (verb == "OPTS") {
      std::string upper = arg;
      for (auto& c : upper) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
      }
      if (upper == "UTF8 ON" || upper == "UTF8") {
        Reply(ctrl, "200", "UTF8 mode enabled.");
      } else {
        Reply(ctrl, "501", "Unsupported option.");
      }
    } else if (verb == "PWD" || verb == "XPWD") {
      std::string quoted;
      for (const char c : session.cwd) {
        quoted.push_back(c);
        if (c == '"') quoted.push_back('"');
      }
      Reply(ctrl, "257", "\"" + quoted + "\" is the current directory.");
    } else if (verb == "CWD" || verb == "XCWD") {
      const std::string path = Normalize(session.cwd, arg);
      struct stat info{};
      if (stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
        session.cwd = path;
        Reply(ctrl, "250", "Directory changed.");
      } else {
        Reply(ctrl, "550", "Directory unavailable.");
      }
    } else if (verb == "CDUP" || verb == "XCUP") {
      session.cwd = Parent(session.cwd);
      Reply(ctrl, "250", "Directory changed.");
    } else if (verb == "TYPE") {
      Reply(ctrl, "200", "Type set.");
    } else if (verb == "MODE") {
      Reply(ctrl, "200", "Mode set.");
    } else if (verb == "STRU") {
      Reply(ctrl, "200", "Structure set.");
    } else if (verb == "PASV") {
      HandlePasv(session);
    } else if (verb == "EPSV") {
      HandleEpsv(session);
    } else if (verb == "PORT") {
      HandlePort(session, arg);
    } else if (verb == "EPRT") {
      Reply(ctrl, "502", "EPRT is not supported; use PASV.");
    } else if (verb == "LIST" || verb == "NLST") {
      std::string target = arg;
      if (!target.empty() && target.front() == '-') {
        const size_t space = target.find(' ');
        target = space == std::string::npos ? std::string()
                                            : target.substr(space + 1);
      }
      SendListing(session, Normalize(session.cwd, target), verb == "NLST");
    } else if (verb == "RETR") {
      SendFile(session, Normalize(session.cwd, arg));
    } else if (verb == "STOR" || verb == "APPE") {
      ReceiveFile(session, Normalize(session.cwd, arg), verb == "APPE");
    } else if (verb == "SIZE") {
      const std::string path = Normalize(session.cwd, arg);
      struct stat info{};
      if (stat(path.c_str(), &info) == 0) {
        char size[32] = {};
        std::snprintf(size, sizeof(size), "%lld",
                      static_cast<long long>(info.st_size));
        SendLine(ctrl, std::string("213 ") + size);
      } else {
        Reply(ctrl, "550", "File unavailable.");
      }
    } else if (verb == "MDTM") {
      const std::string path = Normalize(session.cwd, arg);
      struct stat info{};
      if (stat(path.c_str(), &info) == 0) {
        struct tm tm_value;
        gmtime_r(&info.st_mtime, &tm_value);
        char stamp[32] = {};
        strftime(stamp, sizeof(stamp), "%Y%m%d%H%M%S", &tm_value);
        SendLine(ctrl, std::string("213 ") + stamp);
      } else {
        Reply(ctrl, "550", "File unavailable.");
      }
    } else if (verb == "MKD" || verb == "XMKD") {
      const std::string path = Normalize(session.cwd, arg);
      if (mkdir(path.c_str(), 0755) == 0) {
        Reply(ctrl, "257", "\"" + path + "\" created.");
      } else {
        Reply(ctrl, "550", "Could not create directory.");
      }
    } else if (verb == "RMD" || verb == "XRMD") {
      const std::string path = Normalize(session.cwd, arg);
      if (rmdir(path.c_str()) == 0) {
        Reply(ctrl, "250", "Directory removed.");
      } else {
        Reply(ctrl, "550", "Could not remove directory.");
      }
    } else if (verb == "DELE") {
      const std::string path = Normalize(session.cwd, arg);
      if (unlink(path.c_str()) == 0) {
        Reply(ctrl, "250", "File deleted.");
      } else {
        Reply(ctrl, "550", "Could not delete file.");
      }
    } else if (verb == "RNFR") {
      const std::string path = Normalize(session.cwd, arg);
      struct stat info{};
      if (lstat(path.c_str(), &info) == 0) {
        session.rename_from = path;
        Reply(ctrl, "350", "Ready for destination name.");
      } else {
        Reply(ctrl, "550", "File unavailable.");
      }
    } else if (verb == "RNTO") {
      if (session.rename_from.empty()) {
        Reply(ctrl, "503", "Send RNFR first.");
      } else {
        const std::string path = Normalize(session.cwd, arg);
        if (rename(session.rename_from.c_str(), path.c_str()) == 0) {
          Reply(ctrl, "250", "Rename successful.");
        } else {
          Reply(ctrl, "550", "Could not rename.");
        }
        session.rename_from.clear();
      }
    } else if (verb == "REST") {
      char* end = nullptr;
      const long long offset = std::strtoll(arg.c_str(), &end, 10);
      if (end != arg.c_str() && offset >= 0) {
        session.rest = offset;
        Reply(ctrl, "350", "Restart position accepted.");
      } else {
        Reply(ctrl, "501", "Invalid restart position.");
      }
    } else if (verb == "ABOR") {
      Reply(ctrl, "226", "Abort successful.");
    } else if (verb == "ALLO") {
      Reply(ctrl, "202", "No storage allocation necessary.");
    } else {
      Reply(ctrl, "502", "Command not implemented.");
    }

    line.clear();
  }

  if (session.pasv_fd >= 0) close(session.pasv_fd);
  close(ctrl);
}

void Usage() {
  std::fprintf(stderr,
               "usage: aera_ftp_server [-p port] [-h]\n"
               "Serves the recovery filesystem with plain FTP.\n"
               "Credentials: user root, password from %s\n",
               kPasswordFile);
}

}  // namespace

int main(int argc, char** argv) {
  int port = kDefaultPort;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "-h") == 0 ||
               std::strcmp(argv[i], "--help") == 0) {
      Usage();
      return 0;
    }
  }
  if (port <= 0 || port > 65535) {
    std::fprintf(stderr, "aera_ftp_server: invalid port %d\n", port);
    return 2;
  }

  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  const int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listener < 0) {
    std::perror("aera_ftp_server: socket");
    return 1;
  }
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(listener, 8) != 0) {
    std::perror("aera_ftp_server: bind/listen");
    close(listener);
    return 1;
  }
  std::fprintf(stderr, "aera_ftp_server listening on port %d\n", port);

  for (;;) {
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    const int client = accept(listener, reinterpret_cast<sockaddr*>(&peer), &len);
    if (client < 0) {
      if (errno == EINTR) continue;
      std::perror("aera_ftp_server: accept");
      break;
    }
    const pid_t pid = fork();
    if (pid == 0) {
      close(listener);
      HandleSession(client, peer);
      _exit(0);
    }
    if (pid < 0) {
      close(client);
      continue;
    }
    close(client);
  }

  close(listener);
  return 0;
}
