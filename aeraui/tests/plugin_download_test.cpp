/* SPDX-License-Identifier: Apache-2.0 */
#include "plugins/download.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <sys/prctl.h>

using namespace aeraui::plugins;
enum class Scenario { kSuccess, kSlow, kStall, kPartialFailure, kOrphanHelper };
static Scenario scenario = Scenario::kSuccess;
extern "C" int __wrap_execv(const char *, char *const *arguments) {
  assert(std::string(arguments[1]) == "wget");
  const char *path = nullptr;
  for (size_t i = 0; arguments[i]; ++i)
    if (std::string(arguments[i]) == "-O") path = arguments[i + 1];
  assert(path);
  if (scenario == Scenario::kOrphanHelper) {
    const pid_t helper = fork();
    assert(helper >= 0);
    if (!helper) for (;;) pause();
    std::ofstream(std::string(path) + ".helper") << helper;
    _exit(0); // The helper keeps the completion pipe open after the leader exits.
  }
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  assert(fd >= 0);
  if (scenario == Scenario::kStall) for (;;) pause();
  const int count = scenario == Scenario::kSlow ? 35 : 4;
  for (int i = 0; i < count; ++i) {
    assert(write(fd, "x", 1) == 1);
    if (scenario == Scenario::kSlow) usleep(30000);
  }
  assert(!close(fd));
  _exit(scenario == Scenario::kPartialFailure ? 1 : 0);
}

int main() {
  assert(!prctl(PR_SET_CHILD_SUBREAPER, 1));
  char temporary[] = "/tmp/aera-plugin-download-XXXXXX";
  assert(mkdtemp(temporary));
  const std::string path = std::string(temporary) + "/download";
  const std::string url = "https://raw.githubusercontent.com/AERA-Plugins/registry/main/catalog.json";
  const transport::DownloadPolicy policy{std::chrono::milliseconds(250), 1};
  Progress progress;
  assert(transport::Download(url, path, 1024, &progress, 10, 90, 4, policy));
  assert(progress.value == 90 && progress.downloaded_bytes == 4);

  scenario = Scenario::kSlow;
  const auto slow_start = std::chrono::steady_clock::now();
  assert(transport::Download(url, path, 1024, &progress, 0, 100, 35, policy));
  assert(std::chrono::steady_clock::now() - slow_start > policy.idle_timeout * 3);
  assert(progress.downloaded_bytes == 35); // Continued progress has no total timeout.

  scenario = Scenario::kPartialFailure;
  assert(!transport::Download(url, path, 1024, &progress, 0, 100, 4, policy));
  assert(access(path.c_str(), F_OK));

  scenario = Scenario::kStall;
  const auto stalled_start = std::chrono::steady_clock::now();
  assert(!transport::Download(url, path, 1024, &progress, 0, 100, 4, policy));
  assert(std::chrono::steady_clock::now() - stalled_start < std::chrono::seconds(2));
  assert(access(path.c_str(), F_OK));

  std::thread cancel([&] { usleep(100000); progress.cancel.store(true); });
  const auto cancelled_start = std::chrono::steady_clock::now();
  assert(!transport::Download(url, path, 1024, &progress, 0, 100, 4,
      {std::chrono::seconds(10), 3}));
  cancel.join();
  assert(std::chrono::steady_clock::now() - cancelled_start < std::chrono::seconds(2));
  progress.cancel.store(false);

  scenario = Scenario::kOrphanHelper;
  assert(!transport::Download(url, path, 1024, &progress, 0, 100, 4, policy));
  pid_t helper = -1;
  std::ifstream(path + ".helper") >> helper;
  assert(helper > 0);
  int status = 0;
  assert(waitpid(helper, &status, 0) == helper && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
  unlink((path + ".helper").c_str());

  // Match recovery's process-wide reaper: completion still works with ECHILD.
  const auto previous_handler = signal(SIGCHLD, SIG_IGN);
  scenario = Scenario::kSuccess;
  assert(transport::Download(url, path, 1024, &progress, 0, 100, 4, policy));
  signal(SIGCHLD, previous_handler);
  unlink(path.c_str());
  rmdir(temporary);
  std::cout << "Plugin downloads: success, slow progress, failed/partial data, inactivity, cancellation, helper cleanup and SIGCHLD passed\n";
}
