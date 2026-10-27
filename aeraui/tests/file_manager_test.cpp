/* Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0 */
#include "file_manager.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace aeraui::file_manager;

static void Write(const fs::path &path, const std::string &value) {
  std::ofstream stream(path, std::ios::binary);
  stream << value;
  assert(stream.good());
}

static std::string Read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

static Snapshot Execute(Operation operation,
                        const std::vector<std::string> &sources,
                        const std::string &destination = {},
                        Conflict conflict = Conflict::kKeepBoth,
                        const std::vector<std::string> &archive_members = {}) {
  Request request;
  request.operation = operation;
  request.sources = sources;
  request.destination = destination;
  request.conflict = conflict;
  request.archive_members = archive_members;
  Progress progress;
  Run(request, progress);
  return progress.Get();
}

int main() {
  char pattern[] = "/tmp/aera-file-manager-test-XXXXXX";
  const fs::path root = mkdtemp(pattern);
  assert(IsArchiveFile("backup.ZIP"));
  assert(IsArchiveFile("images.tar.xz"));
  assert(IsArchiveFile("firmware.rar"));
  assert(!IsArchiveFile("notes.txt"));
  assert(SafeArchiveEntryPath("folder/file.txt"));
  assert(!SafeArchiveEntryPath("../outside"));
  assert(!SafeArchiveEntryPath("folder/../../outside"));
  assert(!SafeArchiveEntryPath("/absolute/path"));
  assert(!SafeArchiveEntryPath("C:\\absolute\\path"));
  assert(!SafeArchiveEntryPath("folder\\..\\outside"));
  const auto search_root = root / "search";
  fs::create_directories(search_root / "nested" / "needle-folder");
  fs::create_directories(search_root / ".hidden-folder");
  Write(search_root / "NEEDLE.TXT", "top");
  Write(search_root / "nested" / "needle.bin", "nested");
  Write(search_root / ".needle", "hidden");
  Write(search_root / ".hidden-folder" / "needle.txt", "hidden child");
  Write(search_root / "unrelated.txt", "needle only in contents");
  Write(search_root / u8"\u041e\u0422\u0427\u0401\u0422.txt", "unicode");
  Write(search_root / u8"\u00dcBER.txt", "unicode");
  assert(symlink(search_root.c_str(), (search_root / "nested" / "loop").c_str()) == 0);
  assert(mkfifo((search_root / "needle-pipe").c_str(), 0600) == 0);
  {
    SearchRequest request;
    request.directory = search_root.string();
    request.query = "needle";
    SearchProgress progress;
    Search(request, progress);
    assert(progress.done.load() && progress.error.empty());
    assert(progress.matches.size() == 3 && !progress.limited);
    for (const auto& match : progress.matches)
      assert(match.path.find("loop/") == std::string::npos);
    request.recursive = false;
    SearchProgress local;
    Search(request, local);
    assert(local.matches.size() == 1);
    request.recursive = true;
    request.hidden = true;
    SearchProgress hidden;
    Search(request, hidden);
    assert(hidden.matches.size() == 5);
    request.query = u8"\u043e\u0442\u0447\u0451\u0442";
    SearchProgress cyrillic;
    Search(request, cyrillic);
    assert(cyrillic.matches.size() == 1);
    request.query = u8"\u00fcber";
    SearchProgress latin;
    Search(request, latin);
    assert(latin.matches.size() == 1);
    request.query = "needle";
    request.maximum_results = 1;
    SearchProgress bounded;
    Search(request, bounded);
    assert(bounded.matches.size() == 1 && bounded.limited);
    SearchProgress cancelled;
    cancelled.cancel.store(true);
    Search(request, cancelled);
    assert(cancelled.done.load() && cancelled.matches.empty());
    request.directory = (search_root / "missing").string();
    SearchProgress missing;
    Search(request, missing);
    assert(missing.done.load() && !missing.error.empty());
  }
  const fs::path source_root = root / "source";
  const fs::path destination_root = root / "destination";
  fs::create_directories(source_root / "folder" / "nested");
  fs::create_directories(destination_root);
  Write(source_root / "folder" / "hello.txt", "hello\n");
  assert(chmod((source_root / "folder" / "hello.txt").c_str(), 0750) == 0);
  const timespec source_times[2] = {{1700000000, 0}, {1700000001, 0}};
  assert(utimensat(AT_FDCWD, (source_root / "folder" / "hello.txt").c_str(),
                   source_times, 0) == 0);
  const char attribute[] = "preserved";
  assert(setxattr((source_root / "folder" / "hello.txt").c_str(),
                  "user.aera", attribute, sizeof(attribute), 0) == 0);
  Write(source_root / "folder" / "nested" / "data.bin",
        std::string("abc\0xyz", 7));
  assert(symlink("hello.txt", (source_root / "folder" / "link").c_str()) == 0);

  auto result = Execute(Operation::kCopy,
                        {(source_root / "folder").string()},
                        destination_root.string());
  assert(result.success);
  assert(Read(destination_root / "folder" / "hello.txt") == "hello\n");
  assert(fs::is_symlink(destination_root / "folder" / "link"));
  struct stat source_stat{}, copied_stat{};
  assert(stat((source_root / "folder" / "hello.txt").c_str(), &source_stat) == 0);
  assert(stat((destination_root / "folder" / "hello.txt").c_str(), &copied_stat) == 0);
  assert((source_stat.st_mode & 07777) == (copied_stat.st_mode & 07777));
  assert(source_stat.st_mtime == copied_stat.st_mtime);
  char copied_attribute[32] = {};
  assert(getxattr((destination_root / "folder" / "hello.txt").c_str(),
                  "user.aera", copied_attribute,
                  sizeof(copied_attribute)) == sizeof(attribute));
  assert(std::string(copied_attribute) == attribute);

  assert(symlink(destination_root.c_str(),
                 (source_root / "destination-link").c_str()) == 0);
  result = Execute(Operation::kCopy,
                   {(source_root / "destination-link").string()},
                   destination_root.string());
  assert(result.success);
  assert(fs::is_symlink(destination_root / "destination-link"));

  result = Execute(Operation::kCopy,
                   {(source_root / "folder").string()},
                   destination_root.string(), Conflict::kKeepBoth);
  assert(result.success);
  assert(fs::exists(destination_root / "folder (1)" / "hello.txt"));

  Write(destination_root / "folder" / "stale.txt", "remove me");
  Write(source_root / "folder" / "hello.txt", "replacement\n");
  result = Execute(Operation::kCopy,
                   {(source_root / "folder").string()},
                   destination_root.string(), Conflict::kReplace);
  assert(result.success);
  assert(Read(destination_root / "folder" / "hello.txt") == "replacement\n");
  assert(!fs::exists(destination_root / "folder" / "stale.txt"));

  fs::create_directories(source_root / "broken");
  fs::create_directories(destination_root / "broken");
  Write(source_root / "broken" / "new.txt", "new");
  Write(destination_root / "broken" / "old.txt", "old");
  assert(mkfifo((source_root / "broken" / "unsupported").c_str(), 0600) == 0);
  result = Execute(Operation::kCopy,
                   {(source_root / "broken").string()},
                   destination_root.string(), Conflict::kReplace);
  assert(!result.success);
  assert(Read(destination_root / "broken" / "old.txt") == "old");
  assert(!fs::exists(destination_root / "broken" / "new.txt"));

  fs::create_directories(source_root / "broken-new");
  Write(source_root / "broken-new" / "partial.txt", "partial");
  assert(mkfifo((source_root / "broken-new" / "unsupported").c_str(), 0600) == 0);
  result = Execute(Operation::kCopy,
                   {(source_root / "broken-new").string()},
                   destination_root.string());
  assert(!result.success);
  assert(!fs::exists(destination_root / "broken-new"));

  Write(source_root / "skip.txt", "new");
  Write(destination_root / "skip.txt", "old");
  result = Execute(Operation::kCopy, {(source_root / "skip.txt").string()},
                   destination_root.string(), Conflict::kSkip);
  assert(result.success && result.skipped == 1);
  assert(Read(destination_root / "skip.txt") == "old");

  std::string error;
  const fs::path note = root / "note.txt";
  assert(WriteTextAtomic(note.string(), "first", error));
  assert(WriteTextAtomic(note.string(), "second", error));
  assert(Read(note) == "second");
  assert(Rename(note.string(), (root / "renamed.txt").string(), error));

  const fs::path log = root / "broken.log";
  Write(log, std::string("plain\n\033[31mred\033[0m\ninvalid: ") +
                 std::string("\xc3\x28", 2));
  std::string display_text;
  bool truncated = false;
  assert(ReadText(log.string(), 1024, display_text, truncated, error));
  assert(!truncated);
  assert(display_text == "plain\nred\ninvalid: ?(");

  const std::string maximum_name(255, 'n');
  const fs::path maximum_path = root / maximum_name;
  assert(WriteTextAtomic(maximum_path.string(), "maximum", error));
  assert(Read(maximum_path) == "maximum");

  fs::create_directories(root / "move-target");
  result = Execute(Operation::kMove,
                   {(destination_root / "folder (1)").string()},
                   (root / "move-target").string());
  assert(result.success);
  assert(!fs::exists(destination_root / "folder (1)"));
  assert(fs::exists(root / "move-target" / "folder (1)" / "hello.txt"));

  struct stat tmp_stat{}, shm_stat{};
  if (stat(root.c_str(), &tmp_stat) == 0 && stat("/dev/shm", &shm_stat) == 0 &&
      tmp_stat.st_dev != shm_stat.st_dev) {
    char cross_pattern[] = "/dev/shm/aera-file-manager-test-XXXXXX";
    const char* cross_directory = mkdtemp(cross_pattern);
    assert(cross_directory != nullptr);
    const fs::path cross_source = root / "cross-device.txt";
    Write(cross_source, "cross-device move");
    result = Execute(Operation::kMove, {cross_source.string()}, cross_directory);
    assert(result.success);
    assert(!fs::exists(cross_source));
    assert(Read(fs::path(cross_directory) / "cross-device.txt") == "cross-device move");
    fs::remove_all(cross_directory);
  }

  result = Execute(Operation::kDelete,
                   {(root / "move-target" / "folder (1)").string()});
  assert(result.success);
  assert(!fs::exists(root / "move-target" / "folder (1)"));

  result = Execute(Operation::kMeasure,
                   {(destination_root / "folder").string()});
  assert(result.success);
  assert(result.result_bytes >= 15);

  if (access("/usr/bin/7z", X_OK) == 0) {
    const fs::path archive_source = root / "archive-source";
    const fs::path extract_target = root / "extract-target";
    fs::create_directories(archive_source / "nested");
    fs::create_directories(extract_target);
    Write(archive_source / "nested" / "one.txt", "one");
    Write(archive_source / "nested" / "two.txt", "two");
    Write(archive_source / "top.txt", "top");
    const fs::path archive = root / "sample.zip";
    const std::string command = "cd " + archive_source.string() +
        " && /usr/bin/7z a -bd -y " + archive.string() + " . >/dev/null";
    assert(std::system(command.c_str()) == 0);
    std::vector<ArchiveEntry> archive_entries;
    std::string archive_error;
    assert(ListArchive(archive.string(), archive_entries, archive_error));
    assert(std::any_of(archive_entries.begin(), archive_entries.end(),
                       [](const ArchiveEntry& entry) {
                         return entry.path == "nested/one.txt" && !entry.directory;
                       }));
    result = Execute(Operation::kExtract, {archive.string()}, extract_target.string(),
                     Conflict::kKeepBoth, {"nested/one.txt", "top.txt"});
    assert(result.success);
    assert(Read(extract_target / "nested" / "one.txt") == "one");
    assert(!fs::exists(extract_target / "nested" / "two.txt"));
    assert(Read(extract_target / "top.txt") == "top");
    result = Execute(Operation::kExtract, {archive.string()}, extract_target.string());
    assert(result.success);
    assert(Read(extract_target / "nested (1)" / "two.txt") == "two");

    const fs::path created_archive = root / "created.zip";
    result = Execute(Operation::kCompress,
                     {(archive_source / "nested").string(),
                      (archive_source / "top.txt").string()},
                     created_archive.string());
    assert(result.success && fs::exists(created_archive));
    archive_entries.clear();
    assert(ListArchive(created_archive.string(), archive_entries, archive_error));
    assert(std::any_of(archive_entries.begin(), archive_entries.end(),
                       [](const ArchiveEntry& entry) {
                         return entry.path == "nested/two.txt";
                       }));
    assert(std::any_of(archive_entries.begin(), archive_entries.end(),
                       [](const ArchiveEntry& entry) {
                         return entry.path == "top.txt";
                       }));
  }

  const fs::path large_source = root / "large.bin";
  const fs::path cancel_target = root / "cancel-target";
  Write(large_source, "x");
  fs::resize_file(large_source, 64 * 1024 * 1024);
  fs::create_directory(cancel_target);
  Request cancel_request;
  cancel_request.operation = Operation::kCopy;
  cancel_request.sources = {large_source.string()};
  cancel_request.destination = cancel_target.string();
  Progress cancel_progress;
  std::thread worker([&] { Run(cancel_request, cancel_progress); });
  while (cancel_progress.completed_bytes.load() == 0 &&
         !cancel_progress.done.load())
    std::this_thread::yield();
  cancel_progress.cancel.store(true);
  worker.join();
  assert(cancel_progress.Get().cancelled);
  assert(!fs::exists(cancel_target / "large.bin"));

  fs::remove_all(root);
  std::cout << "file_manager tests passed\n";
}
