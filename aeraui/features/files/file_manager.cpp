/* Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0 */
#include "file_manager.hpp"
#include "../../../aera_shared_storage.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <openssl/evp.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
#include <cwchar>
#include <cwctype>

extern char** environ;

namespace aeraui::file_manager {
namespace {

struct Totals {
  uint64_t bytes = 0;
  size_t items = 0;
};

std::string Error(const std::string& action, const std::string& path);
std::string SanitizeDisplayText(const std::string& input);
void AddError(Progress& progress, const std::string& message);

struct ArchiveInfo {
  uint64_t bytes = 0;
  size_t entries = 0;
};

const char* ArchiveTool() {
  for (const char* path : {"/system/bin/7za", "/sbin/7za", "/usr/bin/7z",
                           "/usr/bin/7za"})
    if (access(path, X_OK) == 0) return path;
  return nullptr;
}

void ParseArchivePercent(const std::string& text, Progress& progress,
                         uint64_t total_bytes) {
  for (size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '%') continue;
    size_t start = index;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(text[start - 1]))) --start;
    if (start == index) continue;
    const unsigned value = static_cast<unsigned>(std::strtoul(
        text.substr(start, index - start).c_str(), nullptr, 10));
    if (value > 100) continue;
    const unsigned displayed = std::min(95U, value * 95U / 100U);
    progress.percent.store(std::max(progress.percent.load(), displayed));
    if (total_bytes != 0)
      progress.completed_bytes.store(total_bytes * displayed / 100U);
  }
}

bool RunArchiveTool(const std::vector<std::string>& arguments, Progress& progress,
                    std::string& output, size_t output_limit, bool report_progress,
                    uint64_t total_bytes, const std::string& working_directory = {}) {
  const char* tool = ArchiveTool();
  if (tool == nullptr) {
    AddError(progress, "Archive extraction support is unavailable in this build.");
    return false;
  }
  int pipes[2] = {-1, -1};
  if (pipe(pipes) != 0) {
    AddError(progress, Error("Cannot start archive extractor", tool));
    return false;
  }
  fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
  fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
  const int flags = fcntl(pipes[0], F_GETFL, 0);
  if (flags >= 0) fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK);

  const pid_t child = fork();
  if (child == 0) {
    setpgid(0, 0);
    if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) _exit(126);
    const int null_input = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null_input >= 0) dup2(null_input, STDIN_FILENO);
    dup2(pipes[1], STDOUT_FILENO);
    dup2(pipes[1], STDERR_FILENO);
    close(pipes[0]);
    close(pipes[1]);
    if (null_input > STDERR_FILENO) close(null_input);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(tool));
    for (const std::string& argument : arguments)
      argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    execve(tool, argv.data(), environ);
    _exit(127);
  }
  close(pipes[1]);
  if (child < 0) {
    close(pipes[0]);
    AddError(progress, Error("Cannot start archive extractor", tool));
    return false;
  }
  setpgid(child, child);

  bool exited = false;
  bool stream_open = true;
  bool overflow = false;
  int status = 0;
  std::string percent_tail;
  while (!exited || stream_open) {
    if (progress.cancel.load() && !exited) {
      kill(-child, SIGKILL);
      kill(child, SIGKILL);
    }
    pollfd descriptor{pipes[0], static_cast<short>(POLLIN | POLLHUP), 0};
    poll(&descriptor, 1, exited ? 0 : 75);
    if (descriptor.revents & (POLLIN | POLLHUP)) {
      std::array<char, 8192> buffer{};
      while (true) {
        const ssize_t count = read(pipes[0], buffer.data(), buffer.size());
        if (count > 0) {
          const std::string chunk(buffer.data(), static_cast<size_t>(count));
          if (report_progress) {
            ParseArchivePercent(percent_tail + chunk, progress, total_bytes);
            const std::string combined = percent_tail + chunk;
            percent_tail = combined.substr(combined.size() > 16 ? combined.size() - 16 : 0);
          }
          if (output.size() + static_cast<size_t>(count) <= output_limit)
            output.append(buffer.data(), static_cast<size_t>(count));
          else
            overflow = true;
          continue;
        }
        if (count == 0) stream_open = false;
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          stream_open = false;
        break;
      }
    }
    if (!exited) {
      const pid_t waited = waitpid(child, &status, WNOHANG);
      if (waited == child) exited = true;
      else if (waited < 0 && errno != EINTR) {
        exited = true;
        status = -1;
      }
    }
    if (exited && !(descriptor.revents & (POLLIN | POLLHUP))) {
      std::array<char, 8192> buffer{};
      const ssize_t count = read(pipes[0], buffer.data(), buffer.size());
      if (count > 0) {
        if (output.size() + static_cast<size_t>(count) <= output_limit)
          output.append(buffer.data(), static_cast<size_t>(count));
        else
          overflow = true;
      } else if (count == 0 || (count < 0 && errno != EINTR)) {
        stream_open = false;
      }
    }
  }
  close(pipes[0]);
  if (!exited) {
    do { errno = 0; } while (waitpid(child, &status, 0) < 0 && errno == EINTR);
  }
  if (overflow) AddError(progress, "The archive listing is too large to validate safely.");
  return !overflow && !progress.cancel.load() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool ParseArchiveListing(const std::string& listing, ArchiveInfo& info,
                         std::vector<ArchiveEntry>* entries, std::string& error) {
  std::istringstream input(listing);
  std::string line;
  std::string path;
  uint64_t size = 0;
  bool encrypted = false;
  bool link = false;
  bool directory = false;
  auto finish = [&]() -> bool {
    if (path.empty()) return true;
    if (!SafeArchiveEntryPath(path)) {
      error = "Unsafe archive path: " + SanitizeDisplayText(path);
      return false;
    }
    if (encrypted) {
      error = "Password-protected archives are not supported yet.";
      return false;
    }
    if (link) {
      error = "Archives containing symbolic or hard links are not extracted for safety.";
      return false;
    }
    if (info.entries >= 100000 || UINT64_MAX - info.bytes < size) {
      error = "The archive contains too many files or reports an invalid size.";
      return false;
    }
    ++info.entries;
    info.bytes += size;
    if (entries != nullptr) entries->push_back({path, directory, size});
    path.clear();
    size = 0;
    encrypted = false;
    link = false;
    directory = false;
    return true;
  };
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) {
      if (!finish()) return false;
      continue;
    }
    const size_t separator = line.find(" = ");
    if (separator == std::string::npos) continue;
    const std::string key = line.substr(0, separator);
    const std::string value = line.substr(separator + 3);
    if (key == "Path") {
      if (!path.empty() && !finish()) return false;
      path = value;
    } else if (key == "Size") {
      char* end = nullptr;
      errno = 0;
      const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
      if (errno != 0 || end == value.c_str() || *end != '\0') {
        error = "The archive reports an invalid expanded size.";
        return false;
      }
      size = static_cast<uint64_t>(parsed);
    } else if (key == "Encrypted" && value == "+") {
      encrypted = true;
    } else if (key == "Folder" && value == "+") {
      directory = true;
    } else if ((key == "Symbolic Link" || key == "Hard Link") && !value.empty()) {
      link = true;
    }
  }
  if (!finish()) return false;
  if (info.entries == 0) {
    error = "The archive is empty or its format is unsupported.";
    return false;
  }
  return true;
}

bool ValidateExtractedTree(const std::string& path, size_t& entries,
                           Progress& progress) {
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) {
    AddError(progress, Error("Cannot inspect extracted item", path));
    return false;
  }
  if (++entries > 100000) {
    AddError(progress, "The extracted archive contains too many items.");
    return false;
  }
  if (S_ISREG(info.st_mode)) return true;
  if (!S_ISDIR(info.st_mode)) {
    AddError(progress, "The archive created an unsupported file type: " + path);
    return false;
  }
  DIR* directory = opendir(path.c_str());
  if (directory == nullptr) {
    AddError(progress, Error("Cannot inspect extracted folder", path));
    return false;
  }
  bool okay = true;
  while (auto* item = readdir(directory)) {
    if (!std::strcmp(item->d_name, ".") || !std::strcmp(item->d_name, "..")) continue;
    if (!ValidateExtractedTree(Join(path, item->d_name), entries, progress)) {
      okay = false;
      break;
    }
  }
  closedir(directory);
  return okay;
}

std::string Error(const std::string& action, const std::string& path) {
  return action + " " + path + ": " + std::strerror(errno);
}

std::string SanitizeDisplayText(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (size_t index = 0; index < input.size();) {
    const unsigned char first = static_cast<unsigned char>(input[index]);
    if (first < 0x80) {
      if (first == 0x1b && index + 1 < input.size() && input[index + 1] == '[') {
        index += 2;
        while (index < input.size()) {
          const unsigned char value = static_cast<unsigned char>(input[index++]);
          if (value >= 0x40 && value <= 0x7e) break;
        }
        continue;
      }
      if (first == '\n' || first == '\r' || first == '\t' || first >= 0x20)
        output.push_back(static_cast<char>(first));
      else
        output.push_back(' ');
      ++index;
      continue;
    }

    size_t length = 0;
    if (first >= 0xc2 && first <= 0xdf)
      length = 2;
    else if (first >= 0xe0 && first <= 0xef)
      length = 3;
    else if (first >= 0xf0 && first <= 0xf4)
      length = 4;

    bool valid = length != 0 && index + length <= input.size();
    for (size_t offset = 1; valid && offset < length; ++offset) {
      const unsigned char value = static_cast<unsigned char>(input[index + offset]);
      valid = (value & 0xc0) == 0x80;
    }
    if (valid && length == 3) {
      const unsigned char second = static_cast<unsigned char>(input[index + 1]);
      valid = (first != 0xe0 || second >= 0xa0) &&
              (first != 0xed || second < 0xa0);
    }
    if (valid && length == 4) {
      const unsigned char second = static_cast<unsigned char>(input[index + 1]);
      valid = (first != 0xf0 || second >= 0x90) &&
              (first != 0xf4 || second <= 0x8f);
    }
    if (valid) {
      output.append(input, index, length);
      index += length;
    } else {
      output.push_back('?');
      ++index;
    }
  }
  return output;
}

void SetText(Progress& progress, const std::string& status, const std::string& detail = {}) {
  std::lock_guard<std::mutex> lock(progress.text_mutex);
  progress.status = status;
  progress.detail = detail;
}

void UpdatePercent(Progress& progress) {
  const uint64_t total = progress.total_bytes.load();
  const uint64_t complete = progress.completed_bytes.load();
  if (total != 0) {
    progress.percent.store(static_cast<unsigned>(std::min<uint64_t>(99, complete * 100 / total)));
    return;
  }
  const size_t items = progress.total_items.load();
  const size_t done = progress.completed_items.load();
  progress.percent.store(
      items == 0 ? 0 : static_cast<unsigned>(std::min<size_t>(99, done * 100 / items)));
}

void AddError(Progress& progress, const std::string& message) {
  progress.errors.fetch_add(1);
  std::lock_guard<std::mutex> lock(progress.text_mutex);
  if (progress.detail.empty()) progress.detail = message;
}

bool MeasureNode(const std::string& path, Totals& totals, Progress& progress,
                 bool expose_progress) {
  if (progress.cancel.load()) return false;
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) {
    AddError(progress, Error("Cannot inspect", path));
    return false;
  }
  ++totals.items;
  if (S_ISREG(info.st_mode)) totals.bytes += static_cast<uint64_t>(info.st_size);
  if (expose_progress) progress.result_bytes.store(totals.bytes);
  if (!S_ISDIR(info.st_mode)) return true;

  DIR* directory = opendir(path.c_str());
  if (directory == nullptr) {
    AddError(progress, Error("Cannot open", path));
    return false;
  }
  bool okay = true;
  while (auto* item = readdir(directory)) {
    if (!std::strcmp(item->d_name, ".") || !std::strcmp(item->d_name, "..")) continue;
    if (!MeasureNode(Join(path, item->d_name), totals, progress, expose_progress)) {
      okay = false;
      if (progress.cancel.load()) break;
    }
  }
  closedir(directory);
  return okay;
}

bool RemoveNode(const std::string& path, Progress& progress, bool account = true) {
  if (progress.cancel.load()) return false;
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) {
    if (errno == ENOENT) return true;
    AddError(progress, Error("Cannot inspect", path));
    return false;
  }
  if (S_ISDIR(info.st_mode)) {
    DIR* directory = opendir(path.c_str());
    if (directory == nullptr) {
      AddError(progress, Error("Cannot open", path));
      return false;
    }
    bool okay = true;
    while (auto* item = readdir(directory)) {
      if (!std::strcmp(item->d_name, ".") || !std::strcmp(item->d_name, "..")) continue;
      if (!RemoveNode(Join(path, item->d_name), progress, account)) {
        okay = false;
        if (progress.cancel.load()) break;
      }
    }
    closedir(directory);
    if (!okay || progress.cancel.load()) return false;
    if (rmdir(path.c_str()) != 0) {
      AddError(progress, Error("Cannot remove", path));
      return false;
    }
  } else {
    if (unlink(path.c_str()) != 0) {
      AddError(progress, Error("Cannot remove", path));
      return false;
    }
    if (account && S_ISREG(info.st_mode))
      progress.completed_bytes.fetch_add(static_cast<uint64_t>(info.st_size));
  }
  if (account) {
    progress.completed_items.fetch_add(1);
    UpdatePercent(progress);
  }
  return true;
}

bool RemoveNodeQuiet(const std::string& path) {
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) return errno == ENOENT;
  if (!S_ISDIR(info.st_mode)) return unlink(path.c_str()) == 0;

  DIR* directory = opendir(path.c_str());
  if (directory == nullptr) return false;
  bool okay = true;
  while (auto* item = readdir(directory)) {
    if (!std::strcmp(item->d_name, ".") || !std::strcmp(item->d_name, "..")) continue;
    if (!RemoveNodeQuiet(Join(path, item->d_name))) okay = false;
  }
  closedir(directory);
  return okay && rmdir(path.c_str()) == 0;
}

void CopyExtendedAttributes(const std::string& source, const std::string& destination) {
  const ssize_t names_size = llistxattr(source.c_str(), nullptr, 0);
  if (names_size <= 0) return;
  std::vector<char> names(static_cast<size_t>(names_size));
  if (llistxattr(source.c_str(), names.data(), names.size()) != names_size) return;

  size_t offset = 0;
  while (offset < names.size()) {
    const char* name = names.data() + offset;
    const size_t length = std::strlen(name);
    if (length == 0) break;
    const ssize_t value_size = lgetxattr(source.c_str(), name, nullptr, 0);
    if (value_size >= 0) {
      std::vector<unsigned char> value(static_cast<size_t>(value_size));
      if (lgetxattr(source.c_str(), name, value.data(), value.size()) == value_size) {
        lsetxattr(destination.c_str(), name, value.data(), value.size(), 0);
      }
    }
    offset += length + 1;
  }
}

int CreateTemporaryFile(const std::string& destination, const char* label, mode_t mode,
                        std::string& path) {
  const std::string directory = Parent(destination);
  for (unsigned index = 0; index < 10000; ++index) {
    path = Join(directory, "." + std::string(label) + "-" +
                               std::to_string(static_cast<long long>(getpid())) + "-" +
                               std::to_string(index));
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (file >= 0 || errno != EEXIST) return file;
  }
  errno = EEXIST;
  return -1;
}

void SyncParentDirectory(const std::string& path) {
  const int directory = open(Parent(path).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) return;
  fsync(directory);
  close(directory);
}

bool CopyRegular(const std::string& source, const std::string& destination, const struct stat& info,
                 Progress& progress) {
  const int input = open(source.c_str(), O_RDONLY | O_CLOEXEC);
  if (input < 0) {
    AddError(progress, Error("Cannot read", source));
    return false;
  }
  std::string temporary;
  const int output = CreateTemporaryFile(destination, "aera-part", 0600, temporary);
  if (output < 0) {
    AddError(progress, Error("Cannot create", destination));
    close(input);
    return false;
  }

  bool okay = true;
  std::vector<char> buffer(1024 * 1024);
  while (!progress.cancel.load()) {
    ssize_t count = read(input, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      AddError(progress, Error("Cannot read", source));
      okay = false;
      break;
    }
    if (count == 0) break;
    ssize_t offset = 0;
    while (offset < count) {
      ssize_t written = write(output, buffer.data() + offset, static_cast<size_t>(count - offset));
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) {
        AddError(progress, Error("Cannot write", destination));
        okay = false;
        break;
      }
      offset += written;
      progress.completed_bytes.fetch_add(static_cast<uint64_t>(written));
      UpdatePercent(progress);
    }
    if (!okay) break;
  }
  if (progress.cancel.load()) okay = false;
  if (okay && fsync(output) != 0) {
    AddError(progress, Error("Cannot sync", destination));
    okay = false;
  }
  if (okay) {
    fchown(output, info.st_uid, info.st_gid);
    fchmod(output, info.st_mode & 07777);
    const timespec times[2] = { info.st_atim, info.st_mtim };
    futimens(output, times);
  }
  close(output);
  close(input);
  if (okay && rename(temporary.c_str(), destination.c_str()) != 0) {
    AddError(progress, Error("Cannot finish", destination));
    okay = false;
  }
  if (okay) {
    CopyExtendedAttributes(source, destination);
    SyncParentDirectory(destination);
  }
  if (!okay) unlink(temporary.c_str());
  return okay;
}

bool CopyNode(const std::string& source, const std::string& destination, Progress& progress) {
  if (progress.cancel.load()) return false;
  struct stat info{};
  if (lstat(source.c_str(), &info) != 0) {
    AddError(progress, Error("Cannot inspect", source));
    return false;
  }
  SetText(progress, "Copying", source);
  bool okay = true;
  if (S_ISREG(info.st_mode)) {
    okay = CopyRegular(source, destination, info, progress);
  } else if (S_ISLNK(info.st_mode)) {
    std::array<char, PATH_MAX + 1> target{};
    const ssize_t count = readlink(source.c_str(), target.data(), PATH_MAX);
    if (count < 0) {
      AddError(progress, Error("Cannot read link", source));
      okay = false;
    } else {
      target[static_cast<size_t>(count)] = '\0';
      if (symlink(target.data(), destination.c_str()) != 0) {
        AddError(progress, Error("Cannot create link", destination));
        okay = false;
      } else {
        lchown(destination.c_str(), info.st_uid, info.st_gid);
        CopyExtendedAttributes(source, destination);
      }
    }
  } else if (S_ISDIR(info.st_mode)) {
    if (mkdir(destination.c_str(), info.st_mode & 07777) != 0 && errno != EEXIST) {
      AddError(progress, Error("Cannot create folder", destination));
      return false;
    }
    DIR* directory = opendir(source.c_str());
    if (directory == nullptr) {
      AddError(progress, Error("Cannot open", source));
      return false;
    }
    while (auto* item = readdir(directory)) {
      if (!std::strcmp(item->d_name, ".") || !std::strcmp(item->d_name, "..")) continue;
      if (!CopyNode(Join(source, item->d_name), Join(destination, item->d_name), progress)) {
        okay = false;
        if (progress.cancel.load()) break;
      }
    }
    closedir(directory);
    if (okay) {
      chown(destination.c_str(), info.st_uid, info.st_gid);
      chmod(destination.c_str(), info.st_mode & 07777);
      const timespec times[2] = { info.st_atim, info.st_mtim };
      utimensat(AT_FDCWD, destination.c_str(), times, AT_SYMLINK_NOFOLLOW);
      CopyExtendedAttributes(source, destination);
    }
  } else {
    AddError(progress, "Unsupported file type: " + source);
    okay = false;
  }
  if (okay) {
    progress.completed_items.fetch_add(1);
    UpdatePercent(progress);
  }
  return okay;
}

std::string Canonical(const std::string& path) {
  std::array<char, PATH_MAX> resolved{};
  return realpath(path.c_str(), resolved.data()) == nullptr ? std::string()
                                                            : std::string(resolved.data());
}

bool Inside(const std::string& parent, const std::string& candidate) {
  const std::string root = Canonical(parent);
  std::string child = Canonical(candidate);
  if (child.empty()) {
    const std::string base = Canonical(Parent(candidate));
    if (!base.empty()) child = Join(base, BaseName(candidate));
  }
  if (root.empty() || child.empty()) return false;
  return child == root || (child.size() > root.size() && child.compare(0, root.size(), root) == 0 &&
                           child[root.size()] == '/');
}

bool SameNode(const std::string& left, const std::string& right) {
  struct stat left_info{};
  struct stat right_info{};
  return lstat(left.c_str(), &left_info) == 0 && lstat(right.c_str(), &right_info) == 0 &&
         left_info.st_dev == right_info.st_dev && left_info.st_ino == right_info.st_ino;
}

std::string UniquePath(const std::string& path, bool directory) {
  if (!Exists(path)) return path;
  const std::string parent = Parent(path);
  const std::string base = BaseName(path);
  std::string stem = base;
  std::string extension;
  if (!directory) {
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot != 0) {
      stem = base.substr(0, dot);
      extension = base.substr(dot);
    }
  }
  for (unsigned index = 1; index < 10000; ++index) {
    const std::string candidate =
        Join(parent, stem + " (" + std::to_string(index) + ")" + extension);
    if (!Exists(candidate)) return candidate;
  }
  return {};
}

std::string ReplacementBackupPath(const std::string& path) {
  const std::string directory = Parent(path);
  for (unsigned index = 0; index < 10000; ++index) {
    const std::string suffix = ".aera-replaced-" +
                               std::to_string(static_cast<long long>(getpid())) + "-" +
                               std::to_string(index);
    const std::string candidate = Join(directory, suffix);
    if (!Exists(candidate)) return candidate;
  }
  return {};
}

bool PrepareDestination(const std::string& source, std::string& destination, const Request& request,
                        Progress& progress, std::string& replacement_backup) {
  const Metadata source_info = Inspect(source);
  if (!source_info.exists) {
    AddError(progress, "Source no longer exists: " + source);
    return false;
  }
  if (!Exists(destination)) {
    if (source_info.directory && Inside(source, destination)) {
      AddError(progress, "Cannot place a folder inside itself: " + source);
      return false;
    }
    return true;
  }
  if (SameNode(source, destination)) {
    if (request.operation == Operation::kMove) {
      progress.skipped.fetch_add(1);
      return false;
    }
    if (request.conflict != Conflict::kKeepBoth) {
      AddError(progress, "Source and destination are the same: " + source);
      return false;
    }
    destination = UniquePath(destination, source_info.directory);
    if (!destination.empty()) return true;
    AddError(progress, "Cannot choose a unique destination for " + source);
    return false;
  }
  if (source_info.directory && Inside(source, destination)) {
    AddError(progress, "Cannot place a folder inside itself: " + source);
    return false;
  }
  if (request.conflict == Conflict::kSkip) {
    progress.skipped.fetch_add(1);
    return false;
  }
  if (request.conflict == Conflict::kKeepBoth) {
    destination = UniquePath(destination, source_info.directory);
    if (destination.empty()) {
      AddError(progress, "Cannot choose a unique destination for " + source);
      return false;
    }
    return true;
  }
  replacement_backup = ReplacementBackupPath(destination);
  if (replacement_backup.empty()) {
    AddError(progress, "Cannot prepare replacement for " + destination);
    return false;
  }
  if (rename(destination.c_str(), replacement_backup.c_str()) == 0) return true;
  AddError(progress, Error("Cannot prepare replacement for", destination));
  replacement_backup.clear();
  return false;
}

void FinishReplacement(const std::string& backup, Progress& progress) {
  if (backup.empty()) return;
  if (!RemoveNodeQuiet(backup)) AddError(progress, "Cannot remove replacement backup: " + backup);
}

void RollBackReplacement(const std::string& destination, const std::string& backup,
                         Progress& progress) {
  if (!RemoveNodeQuiet(destination) && Exists(destination)) {
    AddError(progress, "Cannot clean incomplete destination: " + destination);
    return;
  }
  if (backup.empty()) return;
  if (rename(backup.c_str(), destination.c_str()) != 0)
    AddError(progress, Error("Cannot restore previous destination", destination));
}

bool ArchiveMemberSelected(const ArchiveEntry& entry,
                           const std::vector<std::string>& members) {
  if (members.empty()) return true;
  for (const std::string& member : members) {
    if (entry.path == member) return true;
    if (entry.path.size() > member.size() &&
        entry.path.compare(0, member.size(), member) == 0 &&
        entry.path[member.size()] == '/')
      return true;
  }
  return false;
}

std::string LastArchiveMessage(const std::string& output) {
  std::istringstream stream(output);
  std::string line;
  std::string last;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = SanitizeDisplayText(line);
    if (!line.empty()) last = line;
  }
  return last;
}

bool CompressionSourceSafe(const std::string& path, Progress& progress) {
  if (progress.cancel.load()) return false;
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) {
    AddError(progress, Error("Cannot inspect", path));
    return false;
  }
  if (S_ISLNK(info.st_mode)) {
    AddError(progress, "Symbolic links cannot be compressed safely: " + path);
    return false;
  }
  if (S_ISREG(info.st_mode)) return true;
  if (!S_ISDIR(info.st_mode)) {
    AddError(progress, "Unsupported file type: " + path);
    return false;
  }
  DIR* directory = opendir(path.c_str());
  if (directory == nullptr) {
    AddError(progress, Error("Cannot open", path));
    return false;
  }
  bool okay = true;
  while (auto* item = readdir(directory)) {
    if (!std::strcmp(item->d_name, ".") || !std::strcmp(item->d_name, "..")) continue;
    if (!CompressionSourceSafe(Join(path, item->d_name), progress)) {
      okay = false;
      break;
    }
  }
  closedir(directory);
  return okay;
}

std::string CommonSourceParent(const std::vector<std::string>& sources) {
  if (sources.empty()) return {};
  std::string common = Canonical(Parent(sources.front()));
  if (common.empty()) return {};
  for (const std::string& source : sources) {
    const std::string path = Canonical(source);
    if (path.empty()) return {};
    while (common != "/" &&
           !(path.size() > common.size() && path.compare(0, common.size(), common) == 0 &&
             path[common.size()] == '/'))
      common = Parent(common);
  }
  return common;
}

void CompressArchive(const Request& request, Progress& progress) {
  if (request.sources.empty()) {
    AddError(progress, "Select at least one file or folder to compress.");
    return;
  }
  if (request.archive_format != "zip" && request.archive_format != "7z") {
    AddError(progress, "This archive format cannot be created.");
    return;
  }
  if (request.destination.empty() || Exists(request.destination)) {
    AddError(progress, "An item with the archive name already exists.");
    return;
  }
  const std::string output_directory = Parent(request.destination);
  const Metadata output_info = Inspect(output_directory);
  if (!output_info.directory || output_info.symlink ||
      access(output_directory.c_str(), W_OK) != 0) {
    AddError(progress, "The archive destination is not writable.");
    return;
  }
  Totals totals;
  for (const std::string& source : request.sources) {
    if (!CompressionSourceSafe(source, progress) ||
        !MeasureNode(source, totals, progress, false))
      return;
  }
  progress.total_bytes.store(totals.bytes);
  progress.total_items.store(totals.items);
  progress.result_bytes.store(totals.bytes);
  struct statvfs space{};
  if (statvfs(output_directory.c_str(), &space) == 0) {
    const uint64_t available = static_cast<uint64_t>(space.f_bavail) * space.f_frsize;
    const uint64_t required = totals.bytes + std::min<uint64_t>(64ULL * 1024ULL * 1024ULL,
                                                                totals.bytes / 20 + 1024 * 1024);
    if (required > available) {
      AddError(progress, "Not enough free space to create this archive.");
      return;
    }
  }
  const std::string working_directory = CommonSourceParent(request.sources);
  if (working_directory.empty()) {
    AddError(progress, "Cannot determine a safe archive source folder.");
    return;
  }
  std::string temporary_template = Join(output_directory, ".aera-compress-XXXXXX");
  std::vector<char> temporary_buffer(temporary_template.begin(), temporary_template.end());
  temporary_buffer.push_back('\0');
  const int placeholder = mkstemp(temporary_buffer.data());
  if (placeholder < 0) {
    AddError(progress, Error("Cannot create temporary archive in", output_directory));
    return;
  }
  close(placeholder);
  unlink(temporary_buffer.data());
  const std::string temporary = temporary_buffer.data();
  struct ArchiveCleanup {
    std::string path;
    bool keep = false;
    ~ArchiveCleanup() { if (!keep) unlink(path.c_str()); }
  } cleanup{temporary, false};

  std::vector<std::string> arguments = {
      "a", "-y", "-bd", "-bb1", "-bsp1", "-bso1", "-bse1",
      request.archive_format == "zip" ? "-tzip" : "-t7z", "-mx=5", temporary};
  for (const std::string& source : request.sources) {
    const std::string canonical = Canonical(source);
    std::string relative = canonical.substr(working_directory == "/" ? 1
                                                                      : working_directory.size() + 1);
    if (relative.empty() || !SafeArchiveEntryPath(relative)) {
      AddError(progress, "Cannot add an unsafe source path to the archive.");
      return;
    }
    arguments.push_back(relative);
  }
  SetText(progress, "Compressing files", BaseName(request.destination));
  std::string output;
  if (!RunArchiveTool(arguments, progress, output, 2U * 1024U * 1024U, true,
                      totals.bytes, working_directory)) {
    if (!progress.cancel.load() && progress.errors.load() == 0) {
      const std::string reason = LastArchiveMessage(output);
      AddError(progress, reason.empty() ? "Archive creation failed." : reason);
    }
    return;
  }
  const int archive = open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
  if (archive < 0 || fsync(archive) != 0) {
    if (archive >= 0) close(archive);
    AddError(progress, Error("Cannot sync new archive", temporary));
    return;
  }
  close(archive);
  if (rename(temporary.c_str(), request.destination.c_str()) != 0) {
    AddError(progress, Error("Cannot save new archive", request.destination));
    return;
  }
  cleanup.keep = true;
  SyncParentDirectory(request.destination);
  aera::shared_storage::Normalize(request.destination);
  progress.completed_bytes.store(totals.bytes);
  progress.completed_items.store(totals.items);
}

void ExtractArchive(const Request& request, Progress& progress) {
  if (request.sources.size() != 1) {
    AddError(progress, "Archive extraction requires exactly one archive.");
    return;
  }
  const std::string& archive = request.sources.front();
  const Metadata source = Inspect(archive);
  const Metadata destination_info = Inspect(request.destination);
  if (!source.regular || source.symlink) {
    AddError(progress, "The selected archive is no longer available.");
    return;
  }
  if (!destination_info.directory || destination_info.symlink ||
      access(request.destination.c_str(), W_OK) != 0) {
    AddError(progress, "The extraction destination is not writable.");
    return;
  }
  struct DestinationCleanup {
    std::string path;
    bool enabled = false;
    bool keep = false;
    ~DestinationCleanup() {
      if (enabled && !keep) rmdir(path.c_str());
    }
  } destination_cleanup{request.destination, request.cleanup_empty_destination, false};

  SetText(progress, "Reading archive", BaseName(archive));
  std::string listing;
  if (!RunArchiveTool({"l", "-slt", "-ba", "-sccUTF-8", archive}, progress, listing,
                      32U * 1024U * 1024U, false, 0)) {
    if (!progress.cancel.load() && progress.errors.load() == 0)
      AddError(progress, "Cannot read this archive.");
    return;
  }
  ArchiveInfo info;
  std::vector<ArchiveEntry> entries;
  std::string validation_error;
  if (!ParseArchiveListing(listing, info, &entries, validation_error)) {
    AddError(progress, validation_error);
    return;
  }
  for (const std::string& member : request.archive_members) {
    if (!SafeArchiveEntryPath(member)) {
      AddError(progress, "An unsafe archive item was selected.");
      return;
    }
  }
  uint64_t selected_bytes = 0;
  size_t selected_items = 0;
  for (const ArchiveEntry& entry : entries) {
    if (!ArchiveMemberSelected(entry, request.archive_members)) continue;
    if (UINT64_MAX - selected_bytes < entry.size) {
      AddError(progress, "The archive reports an invalid expanded size.");
      return;
    }
    selected_bytes += entry.size;
    ++selected_items;
  }
  if (!request.archive_members.empty() && selected_items == 0) {
    AddError(progress, "The selected archive items are no longer available.");
    return;
  }
  progress.total_bytes.store(selected_bytes);
  progress.total_items.store(selected_items);
  progress.result_bytes.store(selected_bytes);

  struct statvfs space{};
  if (statvfs(request.destination.c_str(), &space) == 0) {
    const uint64_t available = static_cast<uint64_t>(space.f_bavail) * space.f_frsize;
    constexpr uint64_t reserve = 64ULL * 1024ULL * 1024ULL;
    if (selected_bytes > available ||
        (available > selected_bytes && available - selected_bytes < reserve)) {
      AddError(progress, "Not enough free space to extract this archive.");
      return;
    }
  }

  std::string stage_template = Join(request.destination, ".aera-extract-XXXXXX");
  std::vector<char> stage_buffer(stage_template.begin(), stage_template.end());
  stage_buffer.push_back('\0');
  char* created = mkdtemp(stage_buffer.data());
  if (created == nullptr) {
    AddError(progress, Error("Cannot create extraction workspace in", request.destination));
    return;
  }
  const std::string stage = created;
  struct StageCleanup {
    std::string path;
    ~StageCleanup() { if (!path.empty()) RemoveNodeQuiet(path); }
  } cleanup{stage};

  std::vector<std::string> arguments = {
      "x", "-y", "-aoa", "-bb1", "-bsp1", "-bso1", "-bse1",
      "-sccUTF-8", "-o" + stage, archive};
  for (const std::string& member : request.archive_members) {
    arguments.push_back("-i!" + member);
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [&member](const ArchiveEntry& entry) {
                                      return entry.path == member;
                                    });
    if (found != entries.end() && found->directory)
      arguments.push_back("-i!" + member + "/*");
  }
  SetText(progress, "Extracting archive", BaseName(archive));
  std::string output;
  if (!RunArchiveTool(arguments, progress, output, 2U * 1024U * 1024U, true,
                      selected_bytes)) {
    if (!progress.cancel.load() && progress.errors.load() == 0) {
      const std::string reason = LastArchiveMessage(output);
      AddError(progress, reason.empty() ? "Archive extraction failed." : reason);
    }
    return;
  }
  size_t extracted_entries = 0;
  if (!ValidateExtractedTree(stage, extracted_entries, progress)) return;

  DIR* directory = opendir(stage.c_str());
  if (directory == nullptr) {
    AddError(progress, Error("Cannot inspect extracted files in", stage));
    return;
  }
  std::vector<std::string> children;
  while (auto* item = readdir(directory)) {
    if (!std::strcmp(item->d_name, ".") || !std::strcmp(item->d_name, "..")) continue;
    children.emplace_back(item->d_name);
  }
  closedir(directory);
  for (const std::string& name : children) {
    if (progress.cancel.load()) return;
    const std::string child = Join(stage, name);
    std::string destination = Join(request.destination, name);
    std::string replacement_backup;
    if (!PrepareDestination(child, destination, request, progress, replacement_backup)) continue;
    aera::shared_storage::RepairOnExit shared_permissions(destination);
    if (rename(child.c_str(), destination.c_str()) == 0) {
      FinishReplacement(replacement_backup, progress);
      continue;
    }
    const int move_error = errno;
    if (move_error == EXDEV && CopyNode(child, destination, progress) &&
        RemoveNodeQuiet(child)) {
      FinishReplacement(replacement_backup, progress);
      continue;
    }
    if (move_error != EXDEV) {
      errno = move_error;
      AddError(progress, Error("Cannot place extracted item", destination));
    }
    RollBackReplacement(destination, replacement_backup, progress);
  }
  progress.completed_bytes.store(selected_bytes);
  progress.completed_items.store(selected_items);
  destination_cleanup.keep = progress.errors.load() == 0 && !progress.cancel.load();
}

bool ZipArchivePath(const std::string& path) {
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".zip") == 0;
}

bool CopyArchiveForEditing(const std::string& archive, std::string& temporary,
                           Progress& progress) {
  struct stat info{};
  if (lstat(archive.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
      S_ISLNK(info.st_mode)) {
    AddError(progress, "The ZIP archive is no longer available.");
    return false;
  }
  const int input = open(archive.c_str(), O_RDONLY | O_CLOEXEC);
  if (input < 0) {
    AddError(progress, Error("Cannot read", archive));
    return false;
  }
  const int output = CreateTemporaryFile(archive, "aera-zip-edit", info.st_mode & 07777,
                                         temporary);
  if (output < 0) {
    close(input);
    AddError(progress, Error("Cannot create an archive workspace beside", archive));
    return false;
  }
  bool okay = true;
  std::vector<char> buffer(1024 * 1024);
  while (!progress.cancel.load()) {
    ssize_t count = read(input, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      AddError(progress, Error("Cannot read", archive));
      okay = false;
      break;
    }
    if (count == 0) break;
    ssize_t offset = 0;
    while (offset < count) {
      const ssize_t written = write(output, buffer.data() + offset,
                                    static_cast<size_t>(count - offset));
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) {
        AddError(progress, Error("Cannot prepare", archive));
        okay = false;
        break;
      }
      offset += written;
    }
    if (!okay) break;
  }
  if (progress.cancel.load()) okay = false;
  if (okay && fsync(output) != 0) {
    AddError(progress, Error("Cannot sync archive workspace", temporary));
    okay = false;
  }
  if (okay) {
    fchown(output, info.st_uid, info.st_gid);
    fchmod(output, info.st_mode & 07777);
  }
  close(output);
  close(input);
  if (!okay) {
    unlink(temporary.c_str());
    temporary.clear();
  }
  return okay;
}

bool CreateArchiveStagePath(const std::string& stage, const std::string& relative,
                            std::string& path, Progress& progress) {
  path = stage;
  if (relative.empty()) return true;
  if (!SafeArchiveEntryPath(relative)) {
    AddError(progress, "The destination inside the ZIP is unsafe.");
    return false;
  }
  size_t start = 0;
  while (start < relative.size()) {
    const size_t slash = relative.find('/', start);
    const std::string part = relative.substr(start, slash - start);
    path = Join(path, part);
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
      AddError(progress, Error("Cannot create archive workspace", path));
      return false;
    }
    const Metadata info = Inspect(path);
    if (!info.directory || info.symlink) {
      AddError(progress, "The archive workspace is not a safe directory.");
      return false;
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return true;
}

bool ValidateEditedArchive(const std::string& archive, Progress& progress) {
  std::string output;
  if (!RunArchiveTool({"t", "-bd", "-bso0", "-bse1", archive}, progress, output,
                      2U * 1024U * 1024U, false, 0)) {
    if (!progress.cancel.load() && progress.errors.load() == 0)
      AddError(progress, "The edited ZIP did not pass its integrity test.");
    return false;
  }
  std::string listing;
  if (!RunArchiveTool({"l", "-slt", "-ba", "-sccUTF-8", archive}, progress, listing,
                      32U * 1024U * 1024U, false, 0)) {
    if (!progress.cancel.load() && progress.errors.load() == 0)
      AddError(progress, "The edited ZIP could not be read back.");
    return false;
  }
  ArchiveInfo info;
  std::string error;
  if (!ParseArchiveListing(listing, info, nullptr, error)) {
    AddError(progress, error);
    return false;
  }
  return true;
}

void EditZipArchive(const Request& request, Progress& progress) {
  const std::string& archive = request.destination;
  if (!ZipArchivePath(archive)) {
    AddError(progress, "Only ZIP archives can be edited.");
    return;
  }
  const Metadata archive_info = Inspect(archive);
  const Metadata parent_info = Inspect(Parent(archive));
  if (!archive_info.regular || archive_info.symlink || !parent_info.directory ||
      parent_info.symlink || access(Parent(archive).c_str(), W_OK) != 0) {
    AddError(progress, "The selected ZIP archive is not writable.");
    return;
  }

  SetText(progress, "Reading ZIP", BaseName(archive));
  std::string listing;
  if (!RunArchiveTool({"l", "-slt", "-ba", "-sccUTF-8", archive}, progress, listing,
                      32U * 1024U * 1024U, false, 0)) {
    if (!progress.cancel.load() && progress.errors.load() == 0)
      AddError(progress, "Cannot read this ZIP archive.");
    return;
  }
  ArchiveInfo listed_info;
  std::vector<ArchiveEntry> entries;
  std::string validation_error;
  if (!ParseArchiveListing(listing, listed_info, &entries, validation_error)) {
    AddError(progress, validation_error);
    return;
  }

  std::string temporary;
  if (!CopyArchiveForEditing(archive, temporary, progress)) return;
  struct TemporaryCleanup {
    std::string path;
    bool keep = false;
    ~TemporaryCleanup() { if (!keep && !path.empty()) unlink(path.c_str()); }
  } temporary_cleanup{temporary, false};

  std::string output;
  if (request.operation == Operation::kArchiveDelete) {
    if (request.archive_members.empty()) {
      AddError(progress, "Select at least one ZIP item to delete.");
      return;
    }
    for (const std::string& member : request.archive_members) {
      if (!SafeArchiveEntryPath(member)) {
        AddError(progress, "An unsafe ZIP item was selected.");
        return;
      }
    }
    std::vector<std::string> selected;
    uint64_t selected_bytes = 0;
    for (const ArchiveEntry& entry : entries) {
      if (!ArchiveMemberSelected(entry, request.archive_members)) continue;
      selected.push_back(entry.path);
      selected_bytes += entry.size;
    }
    if (selected.empty()) {
      AddError(progress, "The selected ZIP items are no longer available.");
      return;
    }
    if (selected.size() == entries.size()) {
      AddError(progress, "A ZIP cannot be left empty. Delete the ZIP itself instead.");
      return;
    }
    progress.total_bytes.store(selected_bytes);
    progress.total_items.store(selected.size());
    SetText(progress, "Removing ZIP items", BaseName(archive));
    // Keep command lines bounded for archives containing very large trees.
    // -spd makes names literal, so brackets and wildcard characters in valid
    // filenames cannot accidentally select neighbouring entries.
    for (size_t first = 0; first < selected.size() && !progress.cancel.load(); first += 256) {
      const size_t last = std::min(selected.size(), first + 256);
      std::vector<std::string> arguments = {
          "d", "-y", "-spd", "-bd", "-bb1", "-bsp1", "-bso1", "-bse1", temporary,
          "--"};
      arguments.insert(arguments.end(), selected.begin() + first, selected.begin() + last);
      output.clear();
      if (!RunArchiveTool(arguments, progress, output, 2U * 1024U * 1024U, true,
                          selected_bytes)) {
        if (!progress.cancel.load() && progress.errors.load() == 0) {
          const std::string reason = LastArchiveMessage(output);
          AddError(progress, reason.empty() ? "Could not remove items from the ZIP." : reason);
        }
        return;
      }
    }
    progress.completed_bytes.store(selected_bytes);
    progress.completed_items.store(selected.size());
  } else {
    if (request.sources.empty()) {
      AddError(progress, "Select at least one file or folder to add.");
      return;
    }
    if (!request.archive_directory.empty() &&
        !SafeArchiveEntryPath(request.archive_directory)) {
      AddError(progress, "The destination inside the ZIP is unsafe.");
      return;
    }
    Totals totals;
    std::set<std::string> names;
    for (const std::string& source : request.sources) {
      if (SameNode(source, archive)) {
        AddError(progress, "A ZIP cannot be added to itself.");
        return;
      }
      if (!CompressionSourceSafe(source, progress) ||
          !MeasureNode(source, totals, progress, false))
        return;
      const std::string name = BaseName(source);
      if (!names.insert(name).second) {
        AddError(progress, "Selected items with the same name cannot be added together.");
        return;
      }
    }
    progress.total_bytes.store(totals.bytes);
    progress.total_items.store(totals.items);
    progress.result_bytes.store(totals.bytes);
    std::string stage_template = Join(Parent(archive), ".aera-zip-add-XXXXXX");
    std::vector<char> stage_buffer(stage_template.begin(), stage_template.end());
    stage_buffer.push_back('\0');
    char* created = mkdtemp(stage_buffer.data());
    if (created == nullptr) {
      AddError(progress, Error("Cannot create ZIP workspace in", Parent(archive)));
      return;
    }
    struct StageCleanup {
      std::string path;
      ~StageCleanup() { if (!path.empty()) RemoveNodeQuiet(path); }
    } stage_cleanup{created};
    std::string target_directory;
    if (!CreateArchiveStagePath(created, request.archive_directory, target_directory,
                                progress))
      return;
    std::vector<std::string> staged;
    for (const std::string& source : request.sources) {
      const std::string destination = Join(target_directory, BaseName(source));
      if (!CopyNode(source, destination, progress)) return;
      staged.push_back(request.archive_directory.empty()
                           ? BaseName(source)
                           : request.archive_directory + "/" + BaseName(source));
    }
    std::vector<std::string> arguments = {
        "a", "-y", "-spd", "-bd", "-bb1", "-bsp1", "-bso1", "-bse1", "-tzip",
        temporary, "--"};
    arguments.insert(arguments.end(), staged.begin(), staged.end());
    SetText(progress, "Adding ZIP items", BaseName(archive));
    if (!RunArchiveTool(arguments, progress, output, 2U * 1024U * 1024U, true,
                        totals.bytes, created)) {
      if (!progress.cancel.load() && progress.errors.load() == 0) {
        const std::string reason = LastArchiveMessage(output);
        AddError(progress, reason.empty() ? "Could not add items to the ZIP." : reason);
      }
      return;
    }
  }

  if (progress.cancel.load() || !ValidateEditedArchive(temporary, progress)) return;
  const int edited = open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
  if (edited < 0 || fsync(edited) != 0) {
    if (edited >= 0) close(edited);
    AddError(progress, Error("Cannot sync edited ZIP", temporary));
    return;
  }
  close(edited);
  if (rename(temporary.c_str(), archive.c_str()) != 0) {
    AddError(progress, Error("Cannot save edited ZIP", archive));
    return;
  }
  temporary_cleanup.keep = true;
  SyncParentDirectory(archive);
  aera::shared_storage::Normalize(archive);
}

}  // namespace

void Progress::Reset() {
  cancel.store(false);
  done.store(false);
  success.store(false);
  cancelled.store(false);
  percent.store(0);
  completed_bytes.store(0);
  total_bytes.store(0);
  result_bytes.store(0);
  completed_items.store(0);
  total_items.store(0);
  errors.store(0);
  skipped.store(0);
  std::lock_guard<std::mutex> lock(text_mutex);
  status.clear();
  detail.clear();
  checksum.clear();
}

Snapshot Progress::Get() const {
  Snapshot value;
  value.percent = percent.load();
  value.done = done.load();
  value.cancelled = cancelled.load();
  value.success = success.load();
  value.completed_bytes = completed_bytes.load();
  value.total_bytes = total_bytes.load();
  value.result_bytes = result_bytes.load();
  value.completed_items = completed_items.load();
  value.total_items = total_items.load();
  value.errors = errors.load();
  value.skipped = skipped.load();
  std::lock_guard<std::mutex> lock(text_mutex);
  value.status = status;
  value.detail = detail;
  value.checksum = checksum;
  return value;
}

Metadata Inspect(const std::string& path) {
  Metadata result;
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) return result;
  result.exists = true;
  result.directory = S_ISDIR(info.st_mode);
  result.regular = S_ISREG(info.st_mode);
  result.symlink = S_ISLNK(info.st_mode);
  result.size = info.st_size < 0 ? 0 : static_cast<uint64_t>(info.st_size);
  result.mode = info.st_mode;
  result.uid = info.st_uid;
  result.gid = info.st_gid;
  result.modified = info.st_mtime;
  if (result.symlink) {
    std::array<char, PATH_MAX + 1> target{};
    const ssize_t count = readlink(path.c_str(), target.data(), PATH_MAX);
    if (count >= 0) {
      target[static_cast<size_t>(count)] = '\0';
      result.link_target = target.data();
    }
  }
  return result;
}

std::string TypeName(const Metadata& metadata) {
  if (metadata.directory) return "Folder";
  if (metadata.symlink) return "Symbolic link";
  if (metadata.regular) return "File";
  return "Special file";
}

std::string PermissionText(mode_t mode) {
  std::string value(9, '-');
  constexpr mode_t masks[] = { S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP,
                               S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH };
  constexpr char letters[] = { 'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x' };
  for (size_t index = 0; index < 9; ++index)
    if ((mode & masks[index]) != 0) value[index] = letters[index];
  char octal[8];
  std::snprintf(octal, sizeof(octal), "%04o", mode & 07777);
  return value + "  (" + octal + ")";
}

std::string BaseName(const std::string& path) {
  if (path.empty()) return {};
  size_t end = path.size();
  while (end > 1 && path[end - 1] == '/') --end;
  const size_t slash = path.rfind('/', end - 1);
  return slash == std::string::npos ? path.substr(0, end) : path.substr(slash + 1, end - slash - 1);
}

std::string Parent(const std::string& path) {
  if (path.empty() || path == "/") return "/";
  size_t end = path.size();
  while (end > 1 && path[end - 1] == '/') --end;
  const size_t slash = path.rfind('/', end - 1);
  return slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
}

std::string Join(const std::string& directory, const std::string& name) {
  return directory.empty() || directory == "/" ? "/" + name : directory + "/" + name;
}

bool ValidName(const std::string& name, std::string& error) {
  if (name.empty())
    error = "Name cannot be empty.";
  else if (name == "." || name == "..")
    error = "This name is reserved.";
  else if (name.find('/') != std::string::npos)
    error = "A name cannot contain a slash.";
  else if (name.size() > NAME_MAX)
    error = "This name is too long.";
  else
    return true;
  return false;
}

bool Exists(const std::string& path) {
  struct stat info{};
  return lstat(path.c_str(), &info) == 0;
}

bool IsArchiveFile(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  for (const char* extension : {".zip", ".rar", ".7z", ".tar", ".tar.gz", ".tgz",
                                ".tar.bz2", ".tbz", ".tbz2", ".tar.xz", ".txz",
                                ".gz", ".bz2", ".xz", ".lzma", ".cab", ".iso",
                                ".cpio", ".apk", ".jar"}) {
    const size_t length = std::strlen(extension);
    if (lower.size() >= length &&
        lower.compare(lower.size() - length, length, extension) == 0)
      return true;
  }
  return false;
}

bool SafeArchiveEntryPath(const std::string& input) {
  if (input.empty() || input.size() > PATH_MAX || input.front() == '/' || input.front() == '\\')
    return false;
  if (input.size() >= 2 && std::isalpha(static_cast<unsigned char>(input[0])) &&
      input[1] == ':')
    return false;
  std::string path = input;
  std::replace(path.begin(), path.end(), '\\', '/');
  std::istringstream components(path);
  std::string component;
  while (std::getline(components, component, '/')) {
    if (component == "..") return false;
    for (unsigned char value : component)
      if (value == 0 || value < 0x20 || value == 0x7f) return false;
  }
  return true;
}

bool ListArchive(const std::string& archive, std::vector<ArchiveEntry>& entries,
                 std::string& error) {
  entries.clear();
  error.clear();
  const Metadata metadata = Inspect(archive);
  if (!metadata.regular || metadata.symlink) {
    error = "Only regular archive files can be opened.";
    return false;
  }
  Progress progress;
  progress.Reset();
  std::string listing;
  if (!RunArchiveTool({"l", "-slt", "-ba", "-sccUTF-8", archive}, progress, listing,
                      32U * 1024U * 1024U, false, 0)) {
    const Snapshot snapshot = progress.Get();
    error = snapshot.detail.empty() ? "Cannot read this archive." : snapshot.detail;
    return false;
  }
  ArchiveInfo info;
  if (!ParseArchiveListing(listing, info, &entries, error)) {
    entries.clear();
    return false;
  }
  std::sort(entries.begin(), entries.end(), [](const ArchiveEntry& left,
                                                const ArchiveEntry& right) {
    return left.path < right.path;
  });
  return true;
}

bool IsTextFile(const std::string& path, uint64_t maximum_bytes) {
  const Metadata metadata = Inspect(path);
  if (!metadata.regular) return false;
  const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (file < 0) return false;
  std::array<unsigned char, 8192> buffer{};
  const size_t sample_size = static_cast<size_t>(std::min<uint64_t>(buffer.size(), maximum_bytes));
  const ssize_t count = read(file, buffer.data(), sample_size);
  close(file);
  if (count < 0) return false;
  for (ssize_t index = 0; index < count; ++index)
    if (buffer[static_cast<size_t>(index)] == 0) return false;
  return true;
}

bool ReadText(const std::string& path, uint64_t maximum_bytes, std::string& text, bool& truncated,
              std::string& error) {
  text.clear();
  truncated = false;
  const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (file < 0) {
    error = Error("Cannot open", path);
    return false;
  }
  std::array<char, 65536> buffer{};
  while (text.size() < maximum_bytes) {
    const size_t wanted = std::min<uint64_t>(buffer.size(), maximum_bytes - text.size());
    ssize_t count = read(file, buffer.data(), wanted);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      error = Error("Cannot read", path);
      close(file);
      return false;
    }
    if (count == 0) break;
    if (std::memchr(buffer.data(), 0, static_cast<size_t>(count)) != nullptr) {
      error = "This is a binary file.";
      close(file);
      return false;
    }
    text.append(buffer.data(), static_cast<size_t>(count));
  }
  char extra;
  truncated = read(file, &extra, 1) == 1;
  close(file);
  text = SanitizeDisplayText(text);
  return true;
}

bool WriteTextAtomic(const std::string& path, const std::string& text, std::string& error) {
  Metadata metadata = Inspect(path);
  if (metadata.exists && !metadata.regular) {
    error = "Only regular files can be edited.";
    return false;
  }
  std::string temporary;
  const int file = CreateTemporaryFile(path, "aera-save",
                                       metadata.exists ? metadata.mode & 07777 : 0644, temporary);
  if (file < 0) {
    error = Error("Cannot create", temporary);
    return false;
  }
  bool okay = true;
  size_t offset = 0;
  while (offset < text.size()) {
    ssize_t count = write(file, text.data() + offset, text.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      error = Error("Cannot write", path);
      okay = false;
      break;
    }
    offset += static_cast<size_t>(count);
  }
  if (okay && fsync(file) != 0) {
    error = Error("Cannot sync", path);
    okay = false;
  }
  if (okay && metadata.exists) {
    fchown(file, metadata.uid, metadata.gid);
    fchmod(file, metadata.mode & 07777);
    CopyExtendedAttributes(path, temporary);
  }
  close(file);
  if (okay && rename(temporary.c_str(), path.c_str()) != 0) {
    error = Error("Cannot replace", path);
    okay = false;
  }
  if (okay) SyncParentDirectory(path);
  if (okay) aera::shared_storage::Normalize(path);
  if (!okay) unlink(temporary.c_str());
  return okay;
}

bool CreateDirectory(const std::string& path, std::string& error) {
  if (mkdir(path.c_str(), 0755) == 0) {
    aera::shared_storage::Normalize(path);
    return true;
  }
  error = Error("Cannot create folder", path);
  return false;
}

bool Rename(const std::string& source, const std::string& destination, std::string& error) {
  if (Exists(destination)) {
    error = "An item with this name already exists.";
    return false;
  }
  if (rename(source.c_str(), destination.c_str()) == 0) {
    aera::shared_storage::Normalize(destination, true);
    return true;
  }
  error = Error("Cannot rename", source);
  return false;
}

namespace {

bool CalculateChecksum(const std::string& path, ChecksumType type,
                       Progress& progress, std::string& checksum,
                       std::string& error) {
  const Metadata metadata = Inspect(path);
  if (!metadata.exists || !metadata.regular) {
    error = "Checksums can only be calculated for regular files.";
    return false;
  }

  const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (file < 0) {
    error = Error("Cannot open file for checksum", path);
    return false;
  }

  struct stat opened{};
  if (fstat(file, &opened) != 0 || !S_ISREG(opened.st_mode)) {
    close(file);
    error = "The selected checksum source is no longer a regular file.";
    return false;
  }

  const EVP_MD* digest =
      type == ChecksumType::kMD5 ? EVP_md5() : EVP_sha256();
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (digest == nullptr || context == nullptr ||
      EVP_DigestInit_ex(context, digest, nullptr) != 1) {
    if (context != nullptr) EVP_MD_CTX_free(context);
    close(file);
    error = "Could not initialize the checksum calculator.";
    return false;
  }

  progress.total_bytes.store(
      opened.st_size > 0 ? static_cast<uint64_t>(opened.st_size) : 0);
  progress.total_items.store(1);
  std::array<unsigned char, 256 * 1024> buffer{};
  bool okay = true;
  while (!progress.cancel.load()) {
    const ssize_t count = read(file, buffer.data(), buffer.size());
    if (count > 0) {
      if (EVP_DigestUpdate(context, buffer.data(),
                           static_cast<size_t>(count)) != 1) {
        error = "Could not update the checksum.";
        okay = false;
        break;
      }
      progress.completed_bytes.fetch_add(static_cast<uint64_t>(count));
      UpdatePercent(progress);
      continue;
    }
    if (count == 0) break;
    if (errno == EINTR) continue;
    error = Error("Cannot read file for checksum", path);
    okay = false;
    break;
  }
  close(file);

  if (progress.cancel.load() || !okay) {
    EVP_MD_CTX_free(context);
    return false;
  }

  std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context, bytes.data(), &length) != 1) {
    EVP_MD_CTX_free(context);
    error = "Could not finalize the checksum.";
    return false;
  }
  EVP_MD_CTX_free(context);

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned int index = 0; index < length; ++index)
    output << std::setw(2) << static_cast<unsigned>(bytes[index]);
  checksum = output.str();
  progress.completed_items.store(1);
  return true;
}

}  // namespace

void Search(const SearchRequest& request, SearchProgress& progress) {
  // Filename case conversion uses a thread-local UTF-8 locale, never the UI's.
  locale_t locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
  locale_t previous = locale ? uselocale(locale) : nullptr;
  auto fold = [](const std::string& text) {
    std::wstring result;
    std::mbstate_t state{};
    for (size_t offset = 0; offset < text.size();) {
      wchar_t character{};
      size_t count = std::mbrtowc(&character, text.data() + offset,
                                text.size() - offset, &state);
      if (count == static_cast<size_t>(-1) || count == static_cast<size_t>(-2)) {
        result.push_back(0x110000 + static_cast<unsigned char>(text[offset++]));
        state = {};
      } else {
        if (count == 0) count = 1;
        result.push_back(static_cast<wchar_t>(std::towlower(character)));
        offset += count;
      }
    }
    return result;
  };
  const auto query = fold(request.query);
  struct Pending { std::string path; unsigned depth; };
  std::vector<Pending> pending{{request.directory, 0}};
  std::set<std::pair<dev_t, ino_t>> visited;
  if (query.empty() || request.maximum_results == 0) pending.clear();
  while (!pending.empty() && !progress.cancel.load()) {
    auto current = std::move(pending.back());
    pending.pop_back();
    DIR* directory = opendir(current.path.c_str());
    if (!directory) {
      progress.skipped.fetch_add(1);
      if (current.depth == 0) progress.error = std::strerror(errno);
      continue;
    }
    struct stat identity{};
    if (fstat(dirfd(directory), &identity) != 0 ||
        !visited.emplace(identity.st_dev, identity.st_ino).second) {
      closedir(directory);
      continue;
    }
    while (!progress.cancel.load()) {
      errno = 0;
      auto* item = readdir(directory);
      if (!item) {
        if (errno) progress.skipped.fetch_add(1);
        break;
      }
      if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
      if (item->d_name[0] == '.' && !request.hidden) continue;
      const std::string path = Join(current.path, item->d_name);
      // These virtual trees cannot contain user files and may change endlessly.
      if (path == "/dev" || path == "/proc" || path == "/sys") continue;
      const Metadata metadata = Inspect(path);
      progress.scanned.fetch_add(1);
      if (!metadata.exists) { progress.skipped.fetch_add(1); continue; }
      if (!metadata.directory && !metadata.regular && !metadata.symlink) continue;
      if (fold(item->d_name).find(query) != std::wstring::npos) {
        progress.matches.push_back({path, metadata});
        progress.found.store(progress.matches.size());
        if (progress.matches.size() >= request.maximum_results) {
          progress.limited = true;
          break;
        }
      }
      if (request.recursive && metadata.directory && !metadata.symlink) {
        if (current.depth < 128 && visited.size() + pending.size() < 50000)
          pending.push_back({path, current.depth + 1});
        else
          progress.limited = true;
      }
    }
    closedir(directory);
    if (progress.matches.size() >= request.maximum_results) break;
  }
  if (locale) {
    uselocale(previous);
    freelocale(locale);
  }
  progress.done.store(true, std::memory_order_release);
}

void Run(const Request& request, Progress& progress) {
  progress.Reset();
  if (request.operation == Operation::kChecksum) {
    if (request.sources.size() != 1) {
      AddError(progress, "Select exactly one file to calculate its checksum.");
    } else {
      const std::string algorithm =
          request.checksum_type == ChecksumType::kMD5 ? "MD5" : "SHA-256";
      SetText(progress, "Calculating " + algorithm,
              BaseName(request.sources.front()));
      std::string checksum;
      std::string error;
      if (CalculateChecksum(request.sources.front(), request.checksum_type,
                            progress, checksum, error)) {
        std::lock_guard<std::mutex> lock(progress.text_mutex);
        progress.checksum = std::move(checksum);
      } else if (!progress.cancel.load()) {
        AddError(progress, error.empty() ? "Checksum calculation failed." : error);
      }
    }
    progress.cancelled.store(progress.cancel.load());
    progress.success.store(!progress.cancel.load() && progress.errors.load() == 0);
    if (!progress.cancel.load() && progress.errors.load() == 0)
      progress.percent.store(100);
    SetText(progress, progress.cancel.load()        ? "Cancelled"
                      : progress.errors.load() == 0 ? "Checksum ready"
                                                    : "Checksum failed");
    progress.done.store(true);
    return;
  }
  if (request.operation == Operation::kCompress) {
    CompressArchive(request, progress);
    progress.cancelled.store(progress.cancel.load());
    progress.success.store(!progress.cancel.load() && progress.errors.load() == 0);
    if (!progress.cancel.load() && progress.errors.load() == 0) progress.percent.store(100);
    SetText(progress, progress.cancel.load()        ? "Cancelled"
                      : progress.errors.load() == 0 ? "Complete"
                                                    : "Compression failed");
    progress.done.store(true);
    return;
  }
  if (request.operation == Operation::kExtract) {
    ExtractArchive(request, progress);
    progress.cancelled.store(progress.cancel.load());
    progress.success.store(!progress.cancel.load() && progress.errors.load() == 0);
    if (!progress.cancel.load() && progress.errors.load() == 0) progress.percent.store(100);
    SetText(progress, progress.cancel.load()        ? "Cancelled"
                      : progress.errors.load() == 0 ? "Complete"
                                                    : "Extraction failed");
    progress.done.store(true);
    return;
  }
  if (request.operation == Operation::kArchiveAdd ||
      request.operation == Operation::kArchiveDelete) {
    EditZipArchive(request, progress);
    progress.cancelled.store(progress.cancel.load());
    progress.success.store(!progress.cancel.load() && progress.errors.load() == 0);
    if (!progress.cancel.load() && progress.errors.load() == 0) progress.percent.store(100);
    SetText(progress, progress.cancel.load()        ? "Cancelled"
                      : progress.errors.load() == 0 ? "Complete"
                                                    : "ZIP update failed");
    progress.done.store(true);
    return;
  }
  SetText(progress, request.operation == Operation::kMeasure ? "Calculating size" : "Preparing");
  Totals totals;
  for (const auto& source : request.sources) {
    MeasureNode(source, totals, progress, request.operation == Operation::kMeasure);
    if (progress.cancel.load()) break;
  }
  progress.total_bytes.store(totals.bytes);
  progress.total_items.store(totals.items);
  if (request.operation == Operation::kMeasure) {
    progress.result_bytes.store(totals.bytes);
    progress.success.store(progress.errors.load() == 0 && !progress.cancel.load());
    progress.cancelled.store(progress.cancel.load());
    progress.percent.store(progress.cancel.load() ? 0 : 100);
    progress.done.store(true);
    return;
  }
  if (progress.cancel.load()) {
    progress.cancelled.store(true);
    progress.done.store(true);
    return;
  }

  for (const auto& source : request.sources) {
    if (progress.cancel.load()) break;
    SetText(progress,
            request.operation == Operation::kDelete ? "Deleting"
            : request.operation == Operation::kMove ? "Moving"
                                                    : "Copying",
            source);
    if (request.operation == Operation::kDelete) {
      RemoveNode(source, progress);
      continue;
    }

    std::string destination = Join(request.destination, BaseName(source));
    aera::shared_storage::RepairOnExit shared_permissions(destination);
    std::string replacement_backup;
    if (!PrepareDestination(source, destination, request, progress, replacement_backup)) continue;
    if (request.operation == Operation::kMove && rename(source.c_str(), destination.c_str()) == 0) {
      Totals moved;
      Progress ignored;
      ignored.Reset();
      MeasureNode(destination, moved, ignored, false);
      progress.completed_bytes.fetch_add(moved.bytes);
      progress.completed_items.fetch_add(moved.items);
      UpdatePercent(progress);
      FinishReplacement(replacement_backup, progress);
      continue;
    }
    const int move_error = errno;
    if (request.operation == Operation::kMove && move_error != EXDEV) {
      errno = move_error;
      AddError(progress, Error("Cannot move", source));
      RollBackReplacement(destination, replacement_backup, progress);
      continue;
    }
    if (!CopyNode(source, destination, progress)) {
      RollBackReplacement(destination, replacement_backup, progress);
      continue;
    }
    if (request.operation == Operation::kMove && !RemoveNode(source, progress, false)) {
      FinishReplacement(replacement_backup, progress);
      continue;
    }
    FinishReplacement(replacement_backup, progress);
  }

  progress.cancelled.store(progress.cancel.load());
  progress.success.store(!progress.cancel.load() && progress.errors.load() == 0);
  if (!progress.cancel.load()) progress.percent.store(100);
  SetText(progress, progress.cancel.load()        ? "Cancelled"
                    : progress.errors.load() == 0 ? "Complete"
                                                  : "Completed with errors");
  progress.done.store(true);
}

}  // namespace aeraui::file_manager
