/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstdint>
#include <string>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace aera::preservation {
struct SplashLayout {
  std::string first, second;
  uint64_t bytes = 0;
};

// Never substitute an unslotted alias for a broken or ambiguous A/B pair.
template <typename Exists, typename Inspect>
SplashLayout FindSplashWith(Exists exists, Inspect inspect) {
  const auto resolve = [&](const std::string &name) {
    for (const char *root : {"/dev/block/bootdevice/by-name/", "/dev/block/by-name/"}) {
      const std::string path = std::string(root) + name;
      if (exists(path)) return path;
    }
    return std::string{};
  };
  SplashLayout layout{resolve("splash_a"), resolve("splash_b"), 0};
  uint64_t bytes = 0, other_bytes = 0, device = 0, other_device = 0;
  if (!layout.first.empty() || !layout.second.empty()) {
    if (layout.first.empty() || layout.second.empty() ||
        !inspect(layout.first, &bytes, &device) ||
        !inspect(layout.second, &other_bytes, &other_device) ||
        !bytes || bytes != other_bytes || device == other_device) return {};
  } else {
    layout.first = resolve("splash");
    if (layout.first.empty() || !inspect(layout.first, &bytes, &device) || !bytes)
      return {};
  }
  layout.bytes = bytes;
  return layout;
}
inline SplashLayout FindSplash() {
  return FindSplashWith([](const std::string &path) {
    struct stat state{};
    return lstat(path.c_str(), &state) == 0;
  }, [](const std::string &path, uint64_t *bytes, uint64_t *device) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat state{};
    const bool valid = fstat(fd, &state) == 0 && S_ISBLK(state.st_mode) &&
        ioctl(fd, BLKGETSIZE64, bytes) == 0 && *bytes != 0;
    close(fd);
    if (valid) *device = static_cast<uint64_t>(state.st_rdev);
    return valid;
  });
}
}  // namespace aera::preservation
