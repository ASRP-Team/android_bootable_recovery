#include "aera_ssh/sshd.hpp"

#ifdef OF_ENABLE_WLAN

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "data.hpp"
#include "aera_remote/aera_remote.hpp"

namespace aera::ssh {
namespace {

constexpr const char* kBinary = "/sbin/dropbear";
constexpr const char* kPidFile = "/tmp/dropbear.pid";
constexpr const char* kLogFile = "/tmp/dropbear.log";
constexpr int kDefaultPort = 22;
constexpr const char* kPasswordFile = "/etc/aera_ssh_password";
constexpr const char* kAuthorizedKeysDir = "/.ssh";
constexpr const char* kAuthorizedKeysFile = "/.ssh/authorized_keys";
constexpr const char* kShellsFile = "/etc/shells";
constexpr const char* kHostKeyDir = "/etc/dropbear";

constexpr const char* kEnabledKey = "aera_sshd_enabled";
constexpr const char* kPortKey = "aera_sshd_port";
constexpr const char* kPasswordKey = "aera_sshd_password";
constexpr const char* kAuthorizedKeysKey = "aera_sshd_authorized_keys";

std::mutex g_lock;
pid_t g_pid = -1;
int g_port = 0;

bool ValidPort(int port) { return port > 0 && port <= 65535; }

std::string HexEncode(const unsigned char* data, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

bool ReadRandom(unsigned char* buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, buf + done, len - done);
        if (n > 0) {
            done += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            close(fd);
            return false;
        }
    }
    close(fd);
    return true;
}

std::string RandomPassword() {
    // Avoid ambiguous characters so the password can be typed from a phone.
    static const char kAlphabet[] =
        "abcdefghijkmnpqrstuvwxyz"
        "ABCDEFGHJKLMNPQRSTUVWXYZ"
        "23456789";
    constexpr size_t kAlphabetLen = sizeof(kAlphabet) - 1;
    unsigned char raw[16] = {};
    if (!ReadRandom(raw, sizeof(raw))) {
        const auto now = static_cast<unsigned long long>(time(nullptr));
        for (size_t i = 0; i < sizeof(raw); i++) {
            raw[i] = static_cast<unsigned char>(now >> ((i % 8) * 8));
        }
    }
    std::string out;
    out.reserve(10);
    for (size_t i = 0; i < 10; i++) {
        out.push_back(kAlphabet[raw[i] % kAlphabetLen]);
    }
    return out;
}

bool EnsureDir(const char* path, mode_t mode) {
    if (mkdir(path, mode) == 0) return true;
    return errno == EEXIST;
}

bool WriteFile(const std::string& path, const std::string& content, mode_t mode) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;
    bool ok = fchmod(fd, mode) == 0;
    size_t done = 0;
    while (ok && done < content.size()) {
        ssize_t n = write(fd, content.data() + done, content.size() - done);
        if (n > 0) {
            done += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            ok = false;
        }
    }
    close(fd);
    return ok;
}

// Password file format: sha256:<salt_hex>:<hash_hex>, hash = SHA256(salt || pw)
bool WritePasswordFile(const std::string& password) {
    unsigned char salt[16] = {};
    if (!ReadRandom(salt, sizeof(salt))) {
        std::memset(salt, 0x5a, sizeof(salt));
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, salt, sizeof(salt));
    SHA256_Update(&ctx, password.data(), password.size());
    unsigned char digest[SHA256_DIGEST_LENGTH] = {};
    SHA256_Final(digest, &ctx);

    std::string content = "sha256:" + HexEncode(salt, sizeof(salt)) + ":" +
                          HexEncode(digest, sizeof(digest)) + "\n";
    return WriteFile(kPasswordFile, content, 0600);
}

bool PrepareEnvironment(const std::string& password) {
    if (!EnsureDir(kHostKeyDir, 0700)) return false;
    if (!EnsureDir(kAuthorizedKeysDir, 0700)) return false;
    if (!WriteFile(kShellsFile, "/system/bin/sh\n/sbin/sh\n/bin/sh\n", 0644)) {
        return false;
    }
    if (!WritePasswordFile(password)) return false;

    std::string keys = DataManager::GetStrValue(kAuthorizedKeysKey);
    if (!keys.empty() && keys.back() != '\n') keys.push_back('\n');
    return WriteFile(kAuthorizedKeysFile, keys, 0600);
}

bool PortListening(int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool ok = connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                            sizeof(addr)) == 0;
    close(fd);
    return ok;
}

bool ProcessAlive(pid_t pid) {
    if (pid <= 0) return false;
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) return false;
    if (r < 0 && errno == ECHILD) {
        // Someone else reaped our child (SIGCHLD ignored); fall back to /proc.
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d", static_cast<int>(pid));
        return access(path, F_OK) == 0;
    }
    return true;
}

void StopInternal() {
    if (g_pid <= 0) return;
    kill(g_pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
        int status = 0;
        pid_t r = waitpid(g_pid, &status, WNOHANG);
        if (r == g_pid || (r < 0 && errno == ECHILD)) {
            g_pid = -1;
            break;
        }
        usleep(100 * 1000);
    }
    if (g_pid > 0) {
        kill(g_pid, SIGKILL);
        waitpid(g_pid, nullptr, 0);
        g_pid = -1;
    }
}

bool StartInternal(int port) {
    if (g_pid > 0) {
        if (ProcessAlive(g_pid)) {
            if (g_port == port) return true;
            StopInternal();
        } else {
            g_pid = -1;
        }
    }

    std::string password = DataManager::GetStrValue(kPasswordKey);
    if (password.empty()) {
        password = RandomPassword();
        DataManager::SetValue(kPasswordKey, password, 1);
    }
    if (!PrepareEnvironment(password)) return false;

    const std::string port_text = std::to_string(port);
    pid_t pid = fork();
    if (pid == 0) {
        int log_fd = open(kLogFile, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            if (log_fd > STDERR_FILENO) close(log_fd);
        }
        execl(kBinary, "dropbear", "-F", "-E", "-R", "-p", port_text.c_str(),
              "-P", kPidFile, static_cast<char*>(nullptr));
        _exit(127);
    }
    if (pid < 0) return false;

    // Wait until Dropbear has bound the port (key generation can take a
    // moment), and bail out early if it exited because the port is busy.
    for (int i = 0; i < 50; i++) {
        usleep(100 * 1000);
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return false;
        if (r < 0 && errno == ECHILD) break;
        if (PortListening(port)) break;
    }

    g_pid = pid;
    g_port = port;
    DataManager::SetValue(kEnabledKey, 1, 1);
    DataManager::SetValue(kPortKey, std::to_string(port), 1);
    return true;
}

}  // namespace

bool Start(int port) {
    if (!ValidPort(port)) port = kDefaultPort;
    std::lock_guard<std::mutex> guard(g_lock);
    return StartInternal(port);
}

bool Stop() {
    std::lock_guard<std::mutex> guard(g_lock);
    StopInternal();
    DataManager::SetValue(kEnabledKey, 0, 1);
    g_port = 0;
    return true;
}

bool Running() {
    std::lock_guard<std::mutex> guard(g_lock);
    return ProcessAlive(g_pid);
}

int Port() {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_port > 0) return g_port;
    const int configured = DataManager::GetIntValue(kPortKey);
    return configured > 0 ? configured : kDefaultPort;
}

std::string Address() { return aera::remote::Address(); }

std::string Password() {
    std::string value = DataManager::GetStrValue(kPasswordKey);
    if (value.empty()) {
        // Present a usable random password before the server is ever started.
        value = RandomPassword();
        SetPassword(value);
    }
    return value;
}

bool SetPassword(const std::string& password) {
    std::string value = password;
    if (value.empty()) value = RandomPassword();
    DataManager::SetValue(kPasswordKey, value, 1);
    return WritePasswordFile(value);
}

bool SetPort(int port) {
    if (!ValidPort(port)) return false;
    DataManager::SetValue(kPortKey, std::to_string(port), 1);
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_pid > 0 && ProcessAlive(g_pid) && g_port != port) {
        StopInternal();
        return StartInternal(port);
    }
    return true;
}

std::string AuthorizedKeys() {
    return DataManager::GetStrValue(kAuthorizedKeysKey);
}

bool SetAuthorizedKeys(const std::string& keys) {
    DataManager::SetValue(kAuthorizedKeysKey, keys, 1);
    std::string content = keys;
    if (!content.empty() && content.back() != '\n') content.push_back('\n');
    if (!EnsureDir(kAuthorizedKeysDir, 0700)) return false;
    return WriteFile(kAuthorizedKeysFile, content, 0600);
}

int KeyCount() {
    const std::string keys = AuthorizedKeys();
    int count = 0;
    size_t start = 0;
    while (start < keys.size()) {
        size_t end = keys.find('\n', start);
        if (end == std::string::npos) end = keys.size();
        std::string line = keys.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line[0] != '#') count++;
    }
    return count;
}

}  // namespace aera::ssh

#endif  // OF_ENABLE_WLAN
