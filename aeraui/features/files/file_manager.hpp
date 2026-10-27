/* Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <sys/types.h>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace aeraui::file_manager {

enum class Operation {
  kCopy,
  kMove,
  kDelete,
  kMeasure,
  kChecksum,
  kExtract,
  kCompress,
  kArchiveAdd,
  kArchiveDelete,
};
enum class ChecksumType { kSHA256, kMD5 };
enum class Conflict { kReplace, kKeepBoth, kSkip };

struct Request {
  Operation operation = Operation::kCopy;
  ChecksumType checksum_type = ChecksumType::kSHA256;
  Conflict conflict = Conflict::kKeepBoth;
  std::vector<std::string> sources;
  std::string destination;
  // Archive member paths are relative to the archive root. An empty list
  // extracts the complete archive.
  std::vector<std::string> archive_members;
  // Folder inside an archive where newly-added sources are placed. Empty is
  // the archive root.
  std::string archive_directory;
  // Writable formats are currently "zip" and "7z".
  std::string archive_format = "zip";
  // Remove a newly-created destination if extraction fails before writing it.
  bool cleanup_empty_destination = false;
};

struct ArchiveEntry {
  std::string path;
  bool directory = false;
  uint64_t size = 0;
};

struct Metadata {
  bool exists = false;
  bool directory = false;
  bool regular = false;
  bool symlink = false;
  uint64_t size = 0;
  mode_t mode = 0;
  uid_t uid = 0;
  gid_t gid = 0;
  std::time_t modified = 0;
  std::string link_target;
};

struct SearchRequest {
  std::string directory;
  std::string query;
  bool recursive = true;
  bool hidden = false;
  size_t maximum_results = 2000;
};

struct SearchMatch {
  std::string path;
  Metadata metadata;
};

struct SearchProgress {
  std::atomic<bool> cancel{false};
  std::atomic<bool> done{false};
  std::atomic<size_t> scanned{0};
  std::atomic<size_t> found{0};
  std::atomic<size_t> skipped{0};
  // Read these only after observing done with acquire ordering.
  bool limited = false;
  std::string error;
  std::vector<SearchMatch> matches;
};

void Search(const SearchRequest& request, SearchProgress& progress);

struct Snapshot {
  unsigned percent = 0;
  bool done = false;
  bool cancelled = false;
  bool success = false;
  uint64_t completed_bytes = 0;
  uint64_t total_bytes = 0;
  uint64_t result_bytes = 0;
  size_t completed_items = 0;
  size_t total_items = 0;
  size_t errors = 0;
  size_t skipped = 0;
  std::string status;
  std::string detail;
  std::string checksum;
};

struct Progress {
  std::atomic<bool> cancel{ false };
  std::atomic<bool> done{ false };
  std::atomic<bool> success{ false };
  std::atomic<bool> cancelled{ false };
  std::atomic<unsigned> percent{ 0 };
  std::atomic<uint64_t> completed_bytes{ 0 };
  std::atomic<uint64_t> total_bytes{ 0 };
  std::atomic<uint64_t> result_bytes{ 0 };
  std::atomic<size_t> completed_items{ 0 };
  std::atomic<size_t> total_items{ 0 };
  std::atomic<size_t> errors{ 0 };
  std::atomic<size_t> skipped{ 0 };
  mutable std::mutex text_mutex;
  std::string status;
  std::string detail;
  std::string checksum;

  void Reset();
  Snapshot Get() const;
};

Metadata Inspect(const std::string& path);
std::string TypeName(const Metadata& metadata);
std::string PermissionText(mode_t mode);
std::string BaseName(const std::string& path);
std::string Parent(const std::string& path);
std::string Join(const std::string& directory, const std::string& name);
bool ValidName(const std::string& name, std::string& error);
bool Exists(const std::string& path);
bool IsArchiveFile(const std::string& name);
bool SafeArchiveEntryPath(const std::string& path);
bool ListArchive(const std::string& archive, std::vector<ArchiveEntry>& entries,
                 std::string& error);
bool IsTextFile(const std::string& path, uint64_t maximum_bytes);
bool ReadText(const std::string& path, uint64_t maximum_bytes, std::string& text, bool& truncated,
              std::string& error);
bool WriteTextAtomic(const std::string& path, const std::string& text, std::string& error);
bool CreateDirectory(const std::string& path, std::string& error);
bool Rename(const std::string& source, const std::string& destination, std::string& error);
void Run(const Request& request, Progress& progress);

}  // namespace aeraui::file_manager
