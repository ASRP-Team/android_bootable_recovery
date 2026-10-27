/* SPDX-License-Identifier: Apache-2.0 */
#include "aera_flash_log.hpp"

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <android-base/properties.h>
#include <ziparchive/zip_writer.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "variables.h"

namespace aera::flashlog {
namespace {
constexpr const char *temporary = "/tmp/AERA/logs/FlashLogs";
std::string Settings() {
  std::ostringstream output;
  size_t bytes = 0;
  for (const auto &entry : DataManager::GetDiagnosticSettings()) {
    const auto line = SingleLine(entry.first) + "=" +
        (SensitiveSetting(entry.first) ? "[REDACTED]" : SingleLine(entry.second)) + "\n";
    if ((bytes += line.size()) > 256 * 1024) { output << "[settings snapshot size limit reached]\n"; break; }
    output << line;
  }
  // These controls must be explicit even on devices where their defaults are
  // runtime-only or where the ABL option is hidden by the device configuration.
  output << "\nKeep AERA installed: " << DataManager::GetIntValue(AERA_PRESERVE_RECOVERY_VAR)
         << "\nKeep current ABL: " << DataManager::GetIntValue(AERA_PRESERVE_ABL_VAR) << '\n';
  if (DataManager::GetIntValue(AERA_PRESERVE_SPLASH_VAR) != 0)
    output << "Keep splash: 1\n";
  return output.str();
}
bool MakePath(const std::string &path, bool media = false) {
  for (size_t end = 1; end <= path.size(); ++end) {
    if (end != path.size() && path[end] != '/') continue;
    const auto parent = path.substr(0, end);
    const int created = mkdir(parent.c_str(), media ? 0770 : 0700);
    if (created && errno != EEXIST) return false;
    if (!created && media) TWFunc::set_media_rw_permissions(parent);
  }
  return true;
}
bool WritableLogs(const std::string &path) {
  auto *partition = PartitionManager.Find_Partition_By_Path(path);
  if (!partition || !partition->Is_Mounted()) return false;
  if (partition->Get_Mount_Point() == "/data" && DataManager::GetIntValue(TW_IS_ENCRYPTED) &&
      !DataManager::GetIntValue(TW_IS_DECRYPTED)) return false;
  struct statvfs space{};
  if (statvfs(partition->Get_Mount_Point().c_str(), &space) ||
      static_cast<uint64_t>(space.f_bavail) * space.f_frsize < 64 * 1024 * 1024) return false;
  return MakePath(path, true) && access(path.c_str(), W_OK) == 0;
}
bool Archive(const std::string &root, const std::string &destination, bool manual) {
  const int fd = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return false;
  FILE *file = fdopen(fd, "wb");
  if (!file) { close(fd); return false; }
  bool okay = true;
  {
    ZipWriter writer(file);
    auto add = [&](const char *name, std::initializer_list<const char *> sources, size_t maximum = 32 * 1024 * 1024) {
      if (!okay) return;
      okay = writer.StartEntry(name, ZipWriter::kCompress | ZipWriter::kDefaultCompression) == 0;
      std::array<char, 65536> buffer{};
      size_t remaining = maximum;
      for (const auto *source : sources) {
        if (!okay) break;
        const int input = open((root + "/" + source).c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (input < 0) continue;
        struct stat st{};
        if (fstat(input, &st) || !S_ISREG(st.st_mode)) { close(input); okay = false; break; }
        while (okay && remaining) {
          const auto count = read(input, buffer.data(), std::min(buffer.size(), remaining));
          if (count < 0 && errno == EINTR) continue;
          if (count < 0) { okay = false; break; }
          if (!count) break;
          remaining -= count;
          okay = writer.WriteBytes(buffer.data(), count) == 0;
        }
        close(input);
      }
      if (okay) okay = writer.FinishEntry() == 0;
    };
    add("summary.txt", {"summary.txt"});
    add("recovery.log", {"recovery.previous.log", "recovery.log"}, manual ? kManualRecoveryLimit : 32 * 1024 * 1024);
    add("logcat.txt", {"logcat.previous.txt", "logcat.txt"}, manual ? kManualLogcatLimit : 32 * 1024 * 1024);
    add("kernel.log", {"kernel.previous.log", "kernel.log"}, manual ? kManualKernelLimit : 32 * 1024 * 1024);
    if (manual && access((root + "/state.txt").c_str(), R_OK) == 0)
      add("state.txt", {"state.txt"}, kManualStateLimit);
    if (okay) okay = writer.Finish() == 0;
  }
  if (fflush(file) || fsync(fd)) okay = false;
  if (fclose(file)) okay = false;
  return okay;
}
}  // namespace

std::vector<std::string> PlatformDirectories(bool manual) {
  return {Aera_Logs_Dir + (manual ? "/Diagnostics" : "/FlashLogs"),
      manual ? "/tmp/AERA/logs/Diagnostics" : temporary};
}
Options PlatformOptions(const std::string &file, const std::string &method, const std::string &details, bool manual) {
  Options options;
  options.manual = manual;
  const auto directories = PlatformDirectories(manual);
  const auto &root = directories[0];
  // Never keep /data or removable storage open while an installer may unmount
  // or format it. Resolve the persistent destination only after flashing.
  if (MakePath(directories[1])) options.directory = directories[1];
  if (file.empty() && method.empty() && WritableLogs(root)) options.archive_directory = root;
  if (!options.directory.empty()) {
    chmod(options.directory.c_str(), 0700);
  }
  options.archive = [manual](const std::string &source, const std::string &destination) {
    return Archive(source, destination, manual);
  };
  options.permissions = [](const std::string &path) {
    chmod(path.c_str(), 0660); TWFunc::set_media_rw_permissions(path);
  };
  const time_t context_time = time(nullptr) - 5;
  struct tm context{}; localtime_r(&context_time, &context);
  char context_start[40]; strftime(context_start, sizeof(context_start), "%m-%d %H:%M:%S.000", &context);
  options.logcat = {"/system/bin/logcat", "-b", "all", "-v", "threadtime", "-T", context_start};
  options.kernel = {"/system/bin/toybox", "dmesg"};
  if (manual) {
    options.logcat_limit = kManualLogcatLimit;
    options.recovery_limit = kManualRecoveryLimit;
    options.kernel_limit = kManualKernelLimit;
    options.diagnostics = {
      {"Kernel", {"/system/bin/toybox", "uname", "-a"}},
      {"Uptime and load", {"/system/bin/toybox", "cat", "/proc/uptime", "/proc/loadavg"}},
      {"IPv4 addresses", {"/sbin/busybox", "ip", "-4", "addr", "show"}},
      {"IPv4 routes", {"/sbin/busybox", "ip", "-4", "route", "show", "table", "all"}},
      {"IPv6 addresses", {"/sbin/busybox", "ip", "-6", "addr", "show"}},
      {"IPv6 routes", {"/sbin/busybox", "ip", "-6", "route", "show", "table", "all"}},
      {"Network counters", {"/system/bin/toybox", "cat", "/proc/net/dev"}},
      {"DNS", {"/system/bin/toybox", "cat", "/etc/resolv.conf"}},
      {"Memory", {"/system/bin/toybox", "cat", "/proc/meminfo"}},
      {"Memory and I/O pressure", {"/system/bin/toybox", "cat", "/proc/pressure/memory", "/proc/pressure/io"}},
      {"Mounted filesystems", {"/system/bin/toybox", "cat", "/proc/self/mountinfo"}},
      {"Block devices", {"/system/bin/toybox", "cat", "/proc/partitions"}},
      {"Loaded kernel modules", {"/system/bin/toybox", "cat", "/proc/modules"}},
      {"Processes and threads", {"/system/bin/toybox", "ps", "-A", "-T", "-o", "PID,PPID,TID,USER,STAT,WCHAN,NAME"}}
    };
    const char *network_tool = access("/sbin/busybox", X_OK) == 0 ? "/sbin/busybox" :
        access("/system/bin/busybox", X_OK) == 0 ? "/system/bin/busybox" : "/system/bin/toybox";
    for (auto &command : options.diagnostics)
      if (command.second[0] == "/sbin/busybox") command.second[0] = network_tool;
    // STATUS and SIGNAL_POLL never request saved passwords or private keys.
    const char *wpa = nullptr;
    for (const char *candidate : {"/system/bin/wpa_cli", "/vendor/bin/wpa_cli", "/sbin/wpa_cli"})
      if (access(candidate, X_OK) == 0) { wpa = candidate; break; }
    if (wpa) {
      if (DIR *sockets = opendir("/tmp/recovery/sockets")) {
        unsigned count = 0;
        while (auto *entry = readdir(sockets)) {
          const std::string interface = entry->d_name;
          if (interface.compare(0, 4, "wlan") != 0 || ++count > 4) continue;
          options.diagnostics.insert(options.diagnostics.begin(), {
              "Supplicant status: " + interface,
              {wpa, "-p", "/tmp/recovery/sockets", "-i", interface, "status"}});
          options.diagnostics.insert(options.diagnostics.begin(), {
              "WLAN signal: " + interface,
              {wpa, "-p", "/tmp/recovery/sockets", "-i", interface, "signal_poll"}});
        }
        closedir(sockets);
      }
    }
  }
  if (!file.empty() || !method.empty()) {
    std::ostringstream summary;
    const time_t now = time(nullptr); struct tm utc{}; gmtime_r(&now, &utc);
    char date[40]; strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S UTC", &utc);
    summary << (manual ? "AERA manual diagnostic report\nStarted: " : "AERA flash diagnostic report\nStarted: ") << date
            << "\nFile: " << SingleLine(file) << "\nMethod: " << SingleLine(method)
            << "\nDetails: " << SingleLine(details)
            << "\nAERA version: " << DataManager::GetStrValue(TW_VERSION_VAR)
            << "\nBuild type: " << AERA_BUILD_TYPE << "\nBuild status: " << AERA_BUILD_STATUS
            << "\nCommit: " << AERA_CURRENT_DEV_STR
            << "\nDevice: " << DataManager::GetStrValue(AERA_COMPATIBILITY_DEVICE)
            << "\nSlot before: " << PartitionManager.Get_Active_Slot_Display() << '\n';
    for (const auto *property : {"ro.product.device", "ro.product.model", "ro.boot.hardware",
        "ro.boot.slot_suffix", "ro.boot.serialno", "ro.build.version.release",
        "ro.build.version.security_patch", "ro.vendor.build.security_patch", "ro.crypto.state", "ro.crypto.type"})
      summary << property << '=' << SingleLine(android::base::GetProperty(property, "")) << '\n';
    summary << "\nRecovery settings at start (credentials omitted):\n" << Settings();
    if (manual) summary << "\nManual recording: reproduce the issue, then stop from Quick Settings.\n"
        "Logs can contain personal information. Review them before sharing.\n"
        "Limits: 128 MB logcat, 32 MB recovery log, 8 MB kernel log, 4 MB state snapshots; older output rotates.\n";
    options.summary = summary.str();
    options.final_summary = [] {
      const time_t now = time(nullptr); struct tm utc{}; gmtime_r(&now, &utc);
      char date[40]; strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S UTC", &utc);
      return "Finished: " + std::string(date) + "\nSlot after: " + PartitionManager.Get_Active_Slot_Display() +
          "\n\nRecovery settings at completion (credentials omitted):\n" + Settings();
    };
  }
  return options;
}
}  // namespace aera::flashlog
