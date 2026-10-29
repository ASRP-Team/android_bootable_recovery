/*
 * Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "fox_addons.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <ziparchive/zip_archive.h>

namespace aeraui::fox {
namespace {

constexpr char kBuiltinRoot[] = "/FFiles";
constexpr char kFoxFilesRoot[] = "/sdcard/Fox/FoxFiles";
constexpr char kUserRoot[] = "/sdcard/AERA/plugins/fox";
constexpr char kUserRootLegacy[] = "/sdcard/Fox/addons";
constexpr uint64_t kMaxProperties = 64 * 1024;
constexpr uint64_t kMaxScript = 192 * 1024;
constexpr int kMaxBuiltinDepth = 2;

struct Label {
  const char *basename;
  const char *name;
};

// Friendly names for the add-ons shipped by OrangeFox. Unknown archives keep
// their file name so custom device builds still show up.
const Label kLabels[] = {
    {"OF_DelPass", "Remove lock password"},
    {"OF_Del_Survival", "Remove survival scripts"},
    {"OF_DelFiles", "Remove OrangeFox files"},
    {"OF_DelThemes", "Remove OrangeFox themes"},
    {"OF_reset", "Reset OrangeFox settings"},
    {"OF_backup_settings", "Back up OrangeFox settings"},
    {"OF_initd", "Enable init.d support"},
    {"Magisk", "Install Magisk"},
    {"unrootmagisk", "Uninstall Magisk"},
};

std::string Trim(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])))
    ++first;
  return value.substr(first);
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool EndsWith(const std::string &text, const char *suffix) {
  const size_t length = std::strlen(suffix);
  return text.size() >= length &&
      Lower(text.substr(text.size() - length)) == suffix;
}

std::string BaseName(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Stem(const std::string &name) {
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

// Turns OF_Del_Survival into "OF Del Survival" for archives without a label.
std::string PrettyName(const std::string &basename) {
  std::string text;
  for (char c : basename) {
    if (c == '_' || c == '-') text += ' ';
    else text += c;
  }
  return Trim(text);
}

const Label *LabelFor(const std::string &basename) {
  for (const auto &label : kLabels)
    if (basename == label.basename) return &label;
  return nullptr;
}

int LabelRank(const std::string &basename) {
  for (size_t index = 0; index < sizeof(kLabels) / sizeof(kLabels[0]); ++index)
    if (basename == kLabels[index].basename) return static_cast<int>(index);
  return static_cast<int>(sizeof(kLabels) / sizeof(kLabels[0]));
}

bool FileExists(const std::string &path) {
  struct stat info{};
  return lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool DirectoryExists(const std::string &path) {
  struct stat info{};
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

void ParseProperties(const std::string &text,
                     std::map<std::string, std::string> &properties) {
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find('\n', start);
    const std::string line = Trim(text.substr(
        start, end == std::string::npos ? std::string::npos : end - start));
    const size_t equals = line.find('=');
    if (!line.empty() && line[0] != '#' && equals != std::string::npos) {
      const std::string key = Lower(Trim(line.substr(0, equals)));
      if (!key.empty() && properties.count(key) == 0)
        properties[key] = Trim(line.substr(equals + 1));
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
}

std::string Property(const std::map<std::string, std::string> &properties,
                     const char *key) {
  const auto found = properties.find(key);
  return found == properties.end() ? std::string() : found->second;
}

bool ReadPropertiesFile(const std::string &path,
                        std::map<std::string, std::string> &properties) {
  int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat info{};
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
      static_cast<uint64_t>(info.st_size) > kMaxProperties) {
    close(fd);
    return false;
  }
  std::string text(static_cast<size_t>(info.st_size), '\0');
  size_t done = 0;
  while (done < text.size()) {
    const ssize_t count = read(fd, text.data() + done, text.size() - done);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    done += static_cast<size_t>(count);
  }
  close(fd);
  ParseProperties(text, properties);
  return true;
}

bool ReadArchiveEntry(ZipArchiveHandle archive, const char *name,
                      uint64_t limit, std::string &text) {
  ZipEntry64 entry{};
  if (FindEntry(archive, name, &entry) != 0) return false;
  if (entry.uncompressed_length == 0 || entry.uncompressed_length > limit ||
      entry.uncompressed_length > SIZE_MAX)
    return false;
  text.assign(static_cast<size_t>(entry.uncompressed_length), '\0');
  return ExtractToMemory(archive, &entry,
                         reinterpret_cast<uint8_t *>(text.data()),
                         text.size()) == 0;
}

// Reads addon.prop, then module.prop, from inside the archive. Both are plain
// text manifests used by OrangeFox add-ons and Magisk-style packages. User
// packages also get their installer scripts read so risky commands can be
// spotted before the add-on is allowed to run.
bool InspectArchive(const std::string &path, bool builtin,
                    std::map<std::string, std::string> &properties,
                    std::string &script) {
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  ZipArchiveHandle archive = nullptr;
  if (OpenArchiveFd(fd, path.c_str(), &archive, true) != 0) {
    close(fd);
    return false;
  }
  std::string text;
  for (const char *name : {"addon.prop", "module.prop"}) {
    if (ReadArchiveEntry(archive, name, kMaxProperties, text)) {
      ParseProperties(text, properties);
      break;
    }
  }
  if (!builtin) {
    for (const char *name : {"META-INF/com/google/android/updater-script",
                             "META-INF/com/google/android/update-binary",
                             "addon.sh"}) {
      if (ReadArchiveEntry(archive, name, kMaxScript, text))
        script += Lower(text) + "\n";
    }
  }
  CloseArchive(archive);
  return true;
}

struct ConflictRule {
  const char *keyword;
  const char *reason;
};

constexpr char kRootConflict[] =
    "ASRP already manages root installation through Root Manager, so this "
    "add-on is disabled.";
constexpr char kThemeConflict[] =
    "ASRP already includes a Theme Engine, so this theme add-on is disabled.";
constexpr char kPartitionConflict[] =
    "ASRP already backs up, restores, and wipes partitions, so this partition "
    "add-on is disabled.";
constexpr char kUnsafeReason[] =
    "This add-on uses low-level flashing or OrangeFox-specific commands that "
    "the standard installer cannot run safely, so ASRP disabled it.";

// Simple capability fingerprinting: the add-on only becomes disabled, never
// silently rewritten, so a false positive costs a warning rather than a brick.
const ConflictRule kConflictRules[] = {
    {"magisk", kRootConflict},
    {"supersu", kRootConflict},
    {"superuser", kRootConflict},
    {"kernelsu", kRootConflict},
    {"apatch", kRootConflict},
    {"unroot", kRootConflict},
    {"theme", kThemeConflict},
    {"wipe", kPartitionConflict},
    {"format", kPartitionConflict},
    {"repartition", kPartitionConflict},
    {"partition", kPartitionConflict},
};

const char *const kDangerousCommands[] = {
    "flash_image", "erase_image", "write_raw_image", "install_recovery",
    "set_ofl", "of_", "fox_", "mkfs", "mke2fs", "dd if=",
};

void Classify(Addon &addon, const std::map<std::string, std::string> &properties,
              const std::string &script, bool builtin) {
  const std::string text = Lower(addon.name + " " + addon.description + " " +
      Stem(BaseName(addon.path)) + " " + Property(properties, "id") + " " +
      Property(properties, "name"));
  for (const auto &rule : kConflictRules) {
    if (text.find(rule.keyword) != std::string::npos) {
      addon.risk = Risk::kConflict;
      addon.reason = rule.reason;
      return;
    }
  }
  // Built-in packages come from the recovery image itself and legitimately use
  // OrangeFox helpers, so only inspect script contents for user packages.
  if (builtin) return;
  for (const char *command : kDangerousCommands) {
    if (script.find(command) != std::string::npos) {
      addon.risk = Risk::kUnsafe;
      addon.reason = kUnsafeReason;
      addon.evidence = command;
      return;
    }
  }
}

void AddArchive(const std::string &path, bool builtin,
                std::vector<Addon> &addons, std::set<std::string> &seen,
                const std::map<std::string, std::string> *outer = nullptr) {
  if (!seen.insert(path).second) return;
  const std::string basename = Stem(BaseName(path));
  Addon addon;
  addon.id = path;
  addon.path = path;
  addon.builtin = builtin;
  std::map<std::string, std::string> properties;
  std::string script;
  InspectArchive(path, builtin, properties, script);
  if (outer) {
    for (const auto &[key, value] : *outer) properties[key] = value;
  }
  addon.name = Property(properties, "name");
  addon.version = Property(properties, "version");
  addon.author = Property(properties, "author");
  addon.description = Property(properties, "description");
  if (addon.name.empty()) {
    const Label *label = LabelFor(basename);
    addon.name = label ? label->name : PrettyName(basename);
  }
  if (addon.description.empty()) {
    addon.description = builtin
        ? "Built-in OrangeFox add-on. It runs through the standard ZIP installer."
        : "Legacy Fox add-on from /sdcard/AERA/plugins/fox.";
  }
  if (builtin && addon.name.empty()) addon.name = PrettyName(basename);
  Classify(addon, properties, script, builtin);
  addons.push_back(std::move(addon));
}

void ScanBuiltins(const std::string &directory, int depth,
                  std::vector<Addon> &addons, std::set<std::string> &seen) {
  DIR *dir = opendir(directory.c_str());
  if (!dir) return;
  while (dirent *entry = readdir(dir)) {
    if (entry->d_name[0] == '.') continue;
    const std::string path = directory + "/" + entry->d_name;
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0) continue;
    if (S_ISDIR(info.st_mode)) {
      if (depth > 1) ScanBuiltins(path, depth - 1, addons, seen);
    } else if (S_ISREG(info.st_mode) && EndsWith(entry->d_name, ".zip")) {
      AddArchive(path, true, addons, seen);
    }
  }
  closedir(dir);
}

void ScanFoxFiles(std::vector<Addon> &addons, std::set<std::string> &seen) {
  for (const char *name : {"Magisk.zip", "unrootmagisk.zip", "OF_initd.zip"}) {
    const std::string path = std::string(kFoxFilesRoot) + "/" + name;
    if (FileExists(path)) AddArchive(path, true, addons, seen);
  }
}

// A user add-on may be a bare ZIP or a directory with addon.prop plus the zip
// it describes, matching the layout some OrangeFox ports publish.
void ScanUserRoot(const std::string &directory, std::vector<Addon> &addons,
                  std::set<std::string> &seen) {
  DIR *dir = opendir(directory.c_str());
  if (!dir) return;
  std::map<std::string, std::map<std::string, std::string>> manifests;
  std::vector<std::string> archives;
  while (dirent *entry = readdir(dir)) {
    if (entry->d_name[0] == '.') continue;
    const std::string path = directory + "/" + entry->d_name;
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0) continue;
    if (S_ISREG(info.st_mode) && EndsWith(entry->d_name, ".zip")) {
      archives.push_back(path);
    } else if (S_ISDIR(info.st_mode)) {
      std::map<std::string, std::string> properties;
      ReadPropertiesFile(path + "/addon.prop", properties);
      if (properties.empty()) ReadPropertiesFile(path + "/module.prop", properties);
      manifests[path] = std::move(properties);
    }
  }
  closedir(dir);
  for (const auto &path : archives) AddArchive(path, false, addons, seen);
  for (const auto &[directory_path, properties] : manifests) {
    std::string archive;
    const std::string preferred = directory_path + "/addon.zip";
    if (FileExists(preferred)) {
      archive = preferred;
    } else {
      DIR *inner = opendir(directory_path.c_str());
      if (!inner) continue;
      while (dirent *entry = readdir(inner)) {
        if (entry->d_name[0] == '.' || !EndsWith(entry->d_name, ".zip")) continue;
        archive = directory_path + "/" + entry->d_name;
        break;
      }
      closedir(inner);
    }
    if (archive.empty()) continue;
    AddArchive(archive, false, addons, seen, &properties);
  }
}

}  // namespace

std::vector<Addon> Scan() {
  std::vector<Addon> addons;
  std::set<std::string> seen;
  if (DirectoryExists(kBuiltinRoot))
    ScanBuiltins(kBuiltinRoot, kMaxBuiltinDepth, addons, seen);
  ScanFoxFiles(addons, seen);
  ScanUserRoot(kUserRoot, addons, seen);
  if (DirectoryExists(kUserRootLegacy)) ScanUserRoot(kUserRootLegacy, addons, seen);
  std::sort(addons.begin(), addons.end(), [](const Addon &a, const Addon &b) {
    if (a.builtin != b.builtin) return a.builtin;
    if (a.builtin) {
      const int first = LabelRank(Stem(BaseName(a.path)));
      const int second = LabelRank(Stem(BaseName(b.path)));
      if (first != second) return first < second;
    }
    return Lower(a.name) < Lower(b.name);
  });
  return addons;
}

}  // namespace aeraui::fox
