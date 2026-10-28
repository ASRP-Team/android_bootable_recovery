/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <unistd.h>

namespace aera {

// Snapshot mount identity rather than matching fstab names: firmware can have
// multiple aliases and bind mounts, and device trees use different paths.
struct FlashMount {
  dev_t device = 0;
  std::string root, target, options, type, source, super_options;
  unsigned long propagation = MS_PRIVATE;
  bool removed = false;
};

inline std::string DecodeMountPath(const std::string& value) {
  std::string out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 3 < value.size() &&
        value[i + 1] >= '0' && value[i + 1] <= '7' &&
        value[i + 2] >= '0' && value[i + 2] <= '7' &&
        value[i + 3] >= '0' && value[i + 3] <= '7') {
      out += char((value[i + 1] - '0') * 64 + (value[i + 2] - '0') * 8 + value[i + 3] - '0');
      i += 3;
    } else out += value[i];
  }
  return out;
}

inline bool ParseFlashMounts(const std::string& text, std::vector<FlashMount>& mounts) {
  std::istringstream input(text);
  std::string line;
  mounts.clear();
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    std::istringstream row(line);
    std::string id, parent, device, field;
    FlashMount m;
    if (!(row >> id >> parent >> device >> m.root >> m.target >> m.options)) return false;
    unsigned int major_id, minor_id;
    char extra;
    if (sscanf(device.c_str(), "%u:%u%c", &major_id, &minor_id, &extra) != 2) return false;
    m.device = makedev(major_id, minor_id);
    bool separator = false, shared = false, slave = false;
    while (row >> field) {
      if (field == "-") { separator = true; break; }
      shared |= field.compare(0, 7, "shared:") == 0;
      slave |= field.compare(0, 7, "master:") == 0;
      if (field == "unbindable") m.propagation = MS_UNBINDABLE;
    }
    if (!separator || !(row >> m.type >> m.source >> m.super_options)) return false;
    // Shared+slave groups require restoring a peer relationship, not just flags.
    // Reject such selected mounts during planning instead of silently changing it.
    if (shared && slave) m.propagation = 0;
    else if (shared) m.propagation = MS_SHARED;
    else if (slave) m.propagation = 0; // Cannot recreate the original master relationship.
    m.root = DecodeMountPath(m.root);
    m.target = DecodeMountPath(m.target);
    m.source = DecodeMountPath(m.source);
    if (m.root.empty() || m.root[0] != '/' || m.target.empty() || m.target[0] != '/') return false;
    mounts.push_back(std::move(m));
  }
  return true;
}

inline bool BelowMount(const std::string& path, const std::string& parent) {
  return path == parent || (path.size() > parent.size() &&
      path.compare(0, parent.size(), parent) == 0 && path[parent.size()] == '/');
}

inline bool PlanFlashMounts(const std::vector<FlashMount>& all, const std::set<dev_t>& devices,
                           std::vector<FlashMount>& selected, std::string& error) {
  selected.clear();
  std::vector<std::string> roots;
  for (const auto& m : all) if (devices.count(m.device)) roots.push_back(m.target);
  std::set<std::string> paths;
  for (const auto& m : all) {
    if (!std::any_of(roots.begin(), roots.end(), [&](const auto& root) { return BelowMount(m.target, root); })) continue;
    if (m.target == "/" || !m.propagation || !paths.insert(m.target).second) {
      error = "Cannot safely release stacked/root/shared-slave mount " + m.target;
      return false;
    }
    selected.push_back(m);
  }
  std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
    return a.target.size() > b.target.size();
  });
  return true;
}

inline unsigned long FlashMountFlags(const std::string& options) {
  unsigned long flags = 0;
  std::istringstream input(options);
  std::string option;
  while (std::getline(input, option, ',')) {
    if (option == "ro") flags |= MS_RDONLY;
    else if (option == "nosuid") flags |= MS_NOSUID;
    else if (option == "nodev") flags |= MS_NODEV;
    else if (option == "noexec") flags |= MS_NOEXEC;
    else if (option == "noatime") flags |= MS_NOATIME;
    else if (option == "nodiratime") flags |= MS_NODIRATIME;
    else if (option == "relatime") flags |= MS_RELATIME;
    else if (option == "sync") flags |= MS_SYNCHRONOUS;
    else if (option == "dirsync") flags |= MS_DIRSYNC;
    else if (option == "lazytime") flags |= MS_LAZYTIME;
  }
  return flags;
}

inline std::string FlashFilesystemOptions(const std::string& options) {
  static const std::set<std::string> generic = {"ro", "rw", "nosuid", "nodev", "noexec",
      "noatime", "nodiratime", "relatime", "sync", "dirsync", "lazytime", "seclabel"};
  std::istringstream input(options);
  std::string option, out;
  while (std::getline(input, option, ',')) if (!generic.count(option)) {
    if (!out.empty()) out += ',';
    out += option;
  }
  return out;
}

// Shared parents can propagate a restored bind into another saved alias.
// Reuse that exact mount when its identity and flags match instead of stacking
// a second bind at the alias. Conflicting or already stacked mounts fail closed.
inline bool RestoreFlashMount(const FlashMount& saved) {
  const auto flags = FlashMountFlags(saved.options + "," + saved.super_options);
  const auto options = FlashFilesystemOptions(saved.super_options);
  std::ifstream input("/proc/self/mountinfo");
  if (!input) return false;
  std::stringstream text;
  text << input.rdbuf();
  std::vector<FlashMount> current;
  if (input.bad() || !ParseFlashMounts(text.str(), current) || current.empty()) { errno = EINVAL; return false; }
  struct stat block{};
  if (stat(saved.source.c_str(), &block) || !S_ISBLK(block.st_mode)) return false;
  size_t existing = 0;
  bool matches = true;
  for (const auto& mount : current) {
    if (mount.target != saved.target) continue;
    ++existing;
    matches &= mount.device == block.st_rdev && mount.root == saved.root &&
        mount.type == saved.type && FlashMountFlags(mount.options + "," + mount.super_options) == flags;
  }
  if (existing) {
    if (existing != 1 || !matches) { errno = EBUSY; return false; }
    return mount(nullptr, saved.target.c_str(), nullptr, saved.propagation, nullptr) == 0;
  }
  if (saved.root == "/") {
    if (mount(saved.source.c_str(), saved.target.c_str(), saved.type.c_str(), flags, options.c_str())) return false;
  } else {
    char temporary[] = "/tmp/aera-flash-mount-XXXXXX";
    if (!mkdtemp(temporary)) return false;
    if (mount(saved.source.c_str(), temporary, saved.type.c_str(), flags, options.c_str())) {
      const int error = errno; rmdir(temporary); errno = error; return false;
    }
    bool ok = mount(nullptr, temporary, nullptr, MS_PRIVATE, nullptr) == 0;
    const std::string source = std::string(temporary) + saved.root;
    if (ok) ok = mount(source.c_str(), saved.target.c_str(), nullptr, MS_BIND, nullptr) == 0;
    if (ok && mount(nullptr, saved.target.c_str(), nullptr, MS_BIND | MS_REMOUNT | flags, nullptr)) {
      const int error = errno; umount2(saved.target.c_str(), 0); errno = error; ok = false;
    }
    const int error = errno;
    if (umount2(temporary, 0)) return false;
    rmdir(temporary);
    if (!ok) { errno = error; return false; }
  }
  return mount(nullptr, saved.target.c_str(), nullptr, saved.propagation, nullptr) == 0;
}
}  // namespace aera
