/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "plugin_manager.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aeraui::plugins::transport {
struct DownloadPolicy {
  std::chrono::milliseconds idle_timeout{120000};
  unsigned attempts = 3;
};

// The caller validates the URL and payload bounds. A slow transfer has no total
// time limit: every increase in received bytes renews the inactivity deadline.
inline bool Download(const std::string &url, const std::string &path, uint64_t limit,
                     Progress *progress = nullptr, unsigned progress_start = 0,
                     unsigned progress_end = 0, uint64_t expected_size = 0,
                     DownloadPolicy policy = {}) {
  using Clock = std::chrono::steady_clock;
  if (!limit || !policy.attempts || policy.idle_timeout.count() <= 0) return false;
  for (unsigned attempt = 0; attempt < policy.attempts; ++attempt) {
    unlink(path.c_str());
    if (progress && progress->cancel.load()) return false;
    int completion[2];
    if (pipe2(completion, O_CLOEXEC)) return false;
    const pid_t child = fork();
    if (child < 0) { close(completion[0]); close(completion[1]); return false; }
    if (!child) {
      close(completion[0]);
      // BusyBox can spawn an HTTPS helper. Keep it in the same private group
      // so cancellation also closes helper-owned sockets and pipe descriptors.
      if (setpgid(0, 0) || fcntl(completion[1], F_SETFD, 0)) _exit(126);
      const int null_fd = open("/dev/null", O_RDWR);
      if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
          dup2(null_fd, STDOUT_FILENO) < 0 || dup2(null_fd, STDERR_FILENO) < 0)
        _exit(126);
      if (null_fd > STDERR_FILENO) close(null_fd);
      rlimit size{limit, limit};
      if (setrlimit(RLIMIT_FSIZE, &size)) _exit(126);
      umask(077);
      const char *busybox = access("/sbin/busybox", X_OK) == 0
          ? "/sbin/busybox" : "/system/bin/busybox";
      const char *arguments[] = {busybox, "wget", "-q", "-T", "120", "-t", "1",
                                "-O", path.c_str(), url.c_str(), nullptr};
      execv(busybox, const_cast<char *const *>(arguments));
      _exit(127);
    }
    // Also establish the group from the parent to close the fork/exec race.
    setpgid(child, child);
    close(completion[1]);
    if (progress && expected_size) {
      progress->downloaded_bytes.store(0);
      progress->total_bytes.store(expected_size);
      progress->value.store(progress_start);
    }
    auto last_progress = Clock::now();
    uint64_t previous_bytes = 0;
    bool complete = false;
    for (;;) {
      if (progress && progress->cancel.load()) break;
      pollfd event{completion[0], POLLIN | POLLHUP, 0};
      const int result = poll(&event, 1, 100);
      if (result < 0 && errno == EINTR) continue;
      if (result < 0 || (event.revents & (POLLERR | POLLNVAL))) break;
      if (event.revents & POLLHUP) { complete = true; break; }
      struct stat partial{};
      if (!lstat(path.c_str(), &partial) && S_ISREG(partial.st_mode) && partial.st_size >= 0) {
        const auto bytes = static_cast<uint64_t>(partial.st_size);
        if (bytes > previous_bytes) { previous_bytes = bytes; last_progress = Clock::now(); }
        if (progress && expected_size) {
          const uint64_t shown = std::min(bytes, expected_size);
          progress->downloaded_bytes.store(shown);
          const unsigned span = progress_end > progress_start ? progress_end - progress_start : 0;
          progress->value.store(progress_start + static_cast<unsigned>(shown * span / expected_size));
        }
      }
      if (Clock::now() - last_progress >= policy.idle_timeout) break;
    }
    close(completion[0]);
    if (!complete) kill(-child, SIGKILL);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    // Recovery may already have reaped the leader. Pipe EOF proves that all
    // inheriting download helpers exited; signature/hash checks remain mandatory.
    const bool exited_ok = (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) ||
        (waited < 0 && errno == ECHILD);
    struct stat info{};
    if (complete && exited_ok && !(progress && progress->cancel.load()) &&
        !lstat(path.c_str(), &info) && S_ISREG(info.st_mode) && info.st_size > 0 &&
        static_cast<uint64_t>(info.st_size) <= limit) {
      if (progress && expected_size) {
        progress->downloaded_bytes.store(info.st_size);
        progress->value.store(progress_end);
      }
      return true;
    }
  }
  unlink(path.c_str());
  return false;
}
}  // namespace aeraui::plugins::transport
