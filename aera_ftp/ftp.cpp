#include "aera_ftp/ftp.hpp"

#ifdef OF_ENABLE_WLAN

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aera_ssh/sshd.hpp"
#include "aera_remote/aera_remote.hpp"
#include "data.hpp"

namespace aera::ftp {
namespace {

constexpr const char* kBinary = "/system/bin/aera_ftp_server";
constexpr const char* kLogFile = "/tmp/aera_ftp.log";
constexpr int kDefaultPort = 2121;

constexpr const char* kEnabledKey = "aera_ftp_enabled";
constexpr const char* kPortKey = "aera_ftp_port";

std::mutex g_lock;
pid_t g_pid = -1;
int g_port = 0;

bool ValidPort(int port) { return port > 0 && port <= 65535; }

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

  // Rewrite the shared credential file so FTP works even when the SSH server
  // has never been started in this boot.
  if (!aera::ssh::SetPassword(aera::ssh::Password())) return false;

  const std::string port_text = std::to_string(port);
  pid_t pid = fork();
  if (pid == 0) {
    int log_fd = open(kLogFile, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (log_fd >= 0) {
      dup2(log_fd, STDOUT_FILENO);
      dup2(log_fd, STDERR_FILENO);
      if (log_fd > STDERR_FILENO) close(log_fd);
    }
    execl(kBinary, "aera_ftp_server", "-p", port_text.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) return false;

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

std::string Address() { return aera::remote::Address(); }

}  // namespace aera::ftp

#endif  // OF_ENABLE_WLAN
