#!/usr/bin/env python3
"""Exercise the actual ZIP preserver with ordinary test files, never block devices."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "twrpinstall/twinstall.cpp").read_text()
body = source[source.index("constexpr char kPreservationDirectory[]"):source.index("}  // namespace", source.index("class PartitionPreserver"))]
# ZIP identity parsing is outside the preservation algorithm.
body = body[:body.index("bool ZipEntryContains")] + body[body.index("class PartitionPreserver"):]
with tempfile.TemporaryDirectory(prefix="aera-preservation-test-") as directory:
    base = pathlib.Path(directory)
    body = body.replace('"/tmp/aera-partition-preservation"', '"' + str(base / "backup") + '"')
    body = body.replace("aera::preservation::FindSplash()", "TestFindSplash()")
    prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <openssl/sha.h>
#include "twrpinstall/splash_preservation.hpp"
#include "variables.h"
static std::string base, active = "_a";
static std::map<std::string, int> values;
static unsigned block_reads = 0, block_writes = 0, discoveries = 0;
static std::string Real(const std::string &path) {
  if (path.rfind("/dev/block/", 0) == 0)
    return base + "/" + std::filesystem::path(path).filename().string();
  return path;
}
static int TestOpen(const char *path, int flags, ...) {
  mode_t mode = 0600;
  if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
  if (std::string(path).rfind("/dev/block/", 0) == 0) {
    if ((flags & O_ACCMODE) == O_RDONLY) ++block_reads; else ++block_writes;
  }
  return ::open(Real(path).c_str(), flags, mode);
}
static int TestIoctl(int fd, unsigned long, uint64_t *bytes) {
  struct stat st{}; if (fstat(fd, &st)) return -1;
  *bytes = st.st_size; return 0;
}
struct DataManager { static int GetIntValue(const std::string &key) { return values[key]; } };
struct TWFunc {
  static bool Path_Exists(const std::string &path) { return std::filesystem::exists(Real(path)); }
  static void removeDir(const std::string &path, bool) { std::filesystem::remove_all(path); }
};
struct Manager {
  std::string Get_Active_Slot_Display() { return active == "_a" ? "A" : "B"; }
  void Unlock_Block_Partitions() {}
} PartitionManager;
namespace android::base {
static std::string GetProperty(const std::string &, const std::string &) { return active; }
class unique_fd {
  int fd_;
 public:
  explicit unique_fd(int fd): fd_(fd) {}
  ~unique_fd() { if (fd_ >= 0) close(fd_); }
  int get() const { return fd_; }
};
}
static void Append_Aera_Install_Status(const char *) {}
static void gui_print(const char *, ...) {}
static void gui_print_color(const char *, const char *, ...) {}
#define LOGINFO(...) ((void)0)
static aera::preservation::SplashLayout TestFindSplash() {
  ++discoveries;
  return aera::preservation::FindSplashWith([](const std::string &p) {
    return TWFunc::Path_Exists(p);
  }, [](const std::string &p, uint64_t *bytes, uint64_t *device) {
    const auto real = Real(p); struct stat st{};
    if (stat(real.c_str(), &st)) return false;
    *bytes = st.st_size; *device = st.st_ino; return true;
  });
}
#define open TestOpen
#define ioctl TestIoctl
'''
    suffix = r'''
#undef open
#undef ioctl
static void Put(const char *name, char value) {
  std::ofstream file(base + "/" + name, std::ios::binary | std::ios::trunc);
  file << std::string(4096, value);
}
static void Is(const char *name, char value) {
  std::ifstream file(base + "/" + name, std::ios::binary);
  const std::string data((std::istreambuf_iterator<char>(file)), {});
  assert(data == std::string(4096, value));
}
int main(int argc, char **argv) {
  assert(argc == 2); base = argv[1];
  { PartitionPreserver p; assert(p.Prepare(true)); assert(p.Finalize()); }
  assert(!block_reads && !block_writes && !discoveries); // Off means no extra I/O.
  values[AERA_PRESERVE_SPLASH_VAR] = 1;
  Put("splash", 's');
  { PartitionPreserver p; assert(p.Prepare(true)); Put("splash", 'x'); assert(p.Finalize()); }
  Is("splash", 's');
  const auto writes = block_writes;
  { PartitionPreserver p; assert(p.Prepare(true)); assert(p.Finalize()); }
  assert(block_writes == writes); // An unchanged partition is not rewritten.
  std::filesystem::remove(base + "/splash");
  Put("splash_a", 'a'); Put("splash_b", 'b'); active = "_b";
  { PartitionPreserver p; assert(p.Prepare(true)); Put("splash_a", 'x'); assert(p.Finalize()); }
  Is("splash_a", 'b'); Is("splash_b", 'b');
  assert(!std::filesystem::exists(base + "/backup"));
  // Existing ABL/recovery behavior is exercised unchanged together with splash.
  Put("abl_a", 'c'); Put("abl_b", 'd'); Put("recovery_a", 'r'); Put("recovery_b", 't');
  values[AERA_ABL_PRESERVATION_SUPPORTED] = values[AERA_PRESERVE_ABL_VAR] = 1;
  values[AERA_RECOVERY_PRESERVATION_SUPPORTED] = values[AERA_PRESERVE_RECOVERY_VAR] = 1;
  { PartitionPreserver p; assert(p.Prepare(true));
    Put("abl_a", 'x'); Put("recovery_a", 'x'); Put("splash_a", 'x'); assert(p.Finalize()); }
  Is("abl_a", 'd'); Is("abl_b", 'd'); Is("recovery_a", 't'); Is("recovery_b", 't');
  Is("splash_a", 'b'); Is("splash_b", 'b');
  // AERA recovery updates still opt out of recovery restoration.
  { PartitionPreserver p; assert(p.Prepare(false)); Put("recovery_a", 'u'); assert(p.Finalize()); }
  Is("recovery_a", 'u');
  std::filesystem::remove(base + "/splash_b");
  { PartitionPreserver p; assert(!p.Prepare(true)); assert(p.Finalize()); }
  assert(!std::filesystem::exists(base + "/backup"));
  std::cout << "ZIP preservation: off/no I/O, shared and A/B splash, unchanged/no writes, ABL/recovery and update exception passed.\n";
}
'''
    test = base / "test.cpp"
    test.write_text(prefix + body + suffix)
    binary = base / "test"
    subprocess.run(["c++", "-std=c++17", "-Wno-deprecated-declarations", "-I", str(root),
                    str(test), "-lcrypto", "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(base)], check=True)
