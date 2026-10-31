// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <initializer_list>
#include <sys/stat.h>
#include <unistd.h>

namespace aeraui::telegram {
inline bool RemoveSessionEntryAt(int parent, const char *name, unsigned depth = 0) {
  if (depth > 128) { errno = ELOOP; return false; }
  struct stat st{};
  if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
  if (!S_ISDIR(st.st_mode))
    return unlinkat(parent, name, 0) == 0 || errno == ENOENT;
  int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return false;
  DIR *dir = fdopendir(fd);
  if (!dir) { close(fd); return false; }
  bool ok = true;
  while (ok) {
    errno = 0;
    dirent *entry = readdir(dir);
    if (!entry) { ok = errno == 0; break; }
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    ok = RemoveSessionEntryAt(fd, entry->d_name, depth + 1);
  }
  int saved = errno;
  closedir(dir);
  errno = saved;
  return ok && (unlinkat(parent, name, AT_REMOVEDIR) == 0 || errno == ENOENT);
}

// Caller must stop the isolated worker before invoking this function.
// Keep application API settings; delete only the private session/cache entries.
inline bool ResetLocalSessionAt(int state) {
  for (const char *name : {"database", "files", "avatars", "previews"})
    if (!RemoveSessionEntryAt(state, name)) return false;
  return fsync(state) == 0;
}
}  // namespace aeraui::telegram
