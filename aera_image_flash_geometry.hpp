/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace aera {
// Use kernel identity, not whether the caller used a fstab or raw path.
inline bool BlockRequiresExactCapacity(const std::string& sysfs_node, bool& exact) {
  std::ifstream physical(sysfs_node + "/partition");
  std::ifstream mapper(sysfs_node + "/dm/name");
  if (physical.is_open()) {
    unsigned number = 0;
    std::string extra;
    if (!(physical >> number) || !number || (physical >> extra) || mapper.is_open()) return false;
    exact = false;
    return true;
  }
  std::string name;
  if (!std::getline(mapper, name) || name.empty() || name.find('/') != std::string::npos) return false;
  exact = true;
  return true;
}
inline bool ImageFlashGeometry(int fd, uint64_t& bytes, bool& exact) {
  struct stat st{};
  return !fstat(fd, &st) && S_ISBLK(st.st_mode) &&
      !ioctl(fd, BLKGETSIZE64, &bytes) && bytes &&
      BlockRequiresExactCapacity("/sys/dev/block/" + std::to_string(major(st.st_rdev)) +
          ":" + std::to_string(minor(st.st_rdev)), exact);
}
}
