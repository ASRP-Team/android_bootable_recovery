/* SPDX-License-Identifier: Apache-2.0 */
#include "image_zip.hpp"
#include "payload_arb.hpp"
#include "payload_targets.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <liblp/liblp.h>
#include <openssl/sha.h>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zlib.h>

namespace aeraui::payload {
namespace {
constexpr uint64_t kMaxImage = 1ULL << 40;
uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint16_t Le16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
bool Name(const std::string& name) {
  return !name.empty() && name.size() <= 128 &&
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
}
std::string Trim(std::string s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string Lower(std::string s) {
  for (char& c : s) c = std::tolower(static_cast<unsigned char>(c));
  return s;
}
struct Archive {
  ZipArchiveHandle handle = nullptr;
  ~Archive() { if (handle) CloseArchive(handle); }
};
bool Small(ZipArchiveHandle archive, const char* name, std::string& data, size_t limit = 512 * 1024) {
  ZipEntry64 entry;
  if (FindEntry(archive, name, &entry) || entry.uncompressed_length > limit || (entry.gpbf & 1) ||
      ((entry.unix_mode & S_IFMT) && !S_ISREG(entry.unix_mode))) return false;
  data.assign(entry.uncompressed_length, '\0');
  return ExtractToMemory(archive, &entry, reinterpret_cast<uint8_t*>(data.data()), data.size()) == 0 &&
      crc32(0, reinterpret_cast<const Bytef*>(data.data()), data.size()) == entry.crc32;
}
// libziparchive deliberately disables content CRC checks. Always compute ours.
struct Prefix { std::vector<uint8_t> bytes; };
bool PrefixBytes(const uint8_t* data, size_t bytes, void* cookie) {
  auto& prefix = *static_cast<Prefix*>(cookie);
  const size_t take = std::min<size_t>(64 - prefix.bytes.size(), bytes);
  prefix.bytes.insert(prefix.bytes.end(), data, data + take);
  return prefix.bytes.size() < 64; // Intentional stop; header inspection only.
}
void Bind(SHA256_CTX& hash, const std::string& value) {
  const uint64_t length = value.size();
  SHA256_Update(&hash, &length, sizeof(length));
  SHA256_Update(&hash, value.data(), value.size());
}
bool ReadLayout(const std::string& blob, Info& info) {
  auto metadata = android::fs_mgr::ReadFromImageBlob(blob.data(), blob.size());
  if (!metadata || metadata->partitions.size() > 512 || metadata->groups.size() > 128) return false;
  std::map<std::string, ImageZipPartition> a, b;
  std::map<std::string, uint64_t> groups_a, groups_b;
  for (const auto& group : metadata->groups) {
    auto name = android::fs_mgr::GetPartitionGroupName(group);
    if (name == "default") continue;
    if (!Name(name) || name.size() < 3 || name[name.size()-2] != '_' ||
        (name.back() != 'a' && name.back() != 'b') || group.flags) return false;
    (name.back() == 'a' ? groups_a : groups_b)[name.substr(0, name.size()-2)] = group.maximum_size;
  }
  if (groups_a != groups_b) return false;
  for (const auto& part : metadata->partitions) {
    auto name = android::fs_mgr::GetPartitionName(part);
    if (!Name(name) || name.size() < 3 || name[name.size()-2] != '_' ||
        (name.back() != 'a' && name.back() != 'b') || part.group_index >= metadata->groups.size() ||
        part.num_extents || (part.attributes & ~LP_PARTITION_ATTR_READONLY)) return false;
    auto group = android::fs_mgr::GetPartitionGroupName(metadata->groups[part.group_index]);
    if (group.size() < 3 || group.substr(group.size()-2) != name.substr(name.size()-2)) return false;
    const auto bare = name.substr(0, name.size()-2);
    if (!IsPartitionImageName(bare)) return false;
    auto& entries = name.back() == 'a' ? a : b;
    if (!entries.emplace(bare, ImageZipPartition{bare, group.substr(0, group.size()-2), part.attributes}).second) return false;
  }
  if (a.empty() || a.size() != b.size()) return false;
  for (const auto& [name, part] : a) {
    auto other = b.find(name);
    if (other == b.end() || other->second.group != part.group || other->second.attributes != part.attributes) return false;
    info.image_zip_layout.push_back(part);
  }
  for (const auto& [name, bytes] : groups_a) info.image_zip_groups.push_back({name, bytes});
  return true;
}

// Bounded streaming raw/sparse decoder. DONT_CARE becomes zero bytes, so old
// image data cannot survive and readback has an unambiguous digest.
class ImageWriter {
 public:
  ImageWriter(int fd, const Partition& part,
      std::function<void(uint64_t)> progress) : fd_(fd), part_(part), progress_(std::move(progress)) {
    SHA256_Init(&hash_);
    if (part.sparse) { state_ = Header; need_ = 28; }
  }
  static bool Append(const uint8_t* data, size_t bytes, void* cookie) {
    return static_cast<ImageWriter*>(cookie)->Append(data, bytes);
  }
  bool Finish(const ZipEntry64& entry, std::string& digest) {
    if (encoded_ != entry.uncompressed_length || encoded_crc_ != entry.crc32 ||
        written_ != part_.bytes || (part_.sparse && (state_ != Chunk || !buffer_.empty() ||
        chunks_ != total_chunks_ || (image_crc_ && image_crc_ != output_crc_)))) return false;
    unsigned char bytes[SHA256_DIGEST_LENGTH];
    SHA256_Final(bytes, &hash_);
    digest.assign(reinterpret_cast<char*>(bytes), sizeof(bytes));
    return true;
  }
 private:
  enum State { Raw, Header, SkipHeader, Chunk, SkipChunk, Data, Fill, Crc } state_ = Raw;
  int fd_;
  const Partition& part_;
  std::function<void(uint64_t)> progress_;
  uint64_t written_ = 0, encoded_ = 0, remaining_ = 0, chunk_bytes_ = 0;
  uint32_t encoded_crc_ = 0, output_crc_ = 0, image_crc_ = 0, block_ = 0, total_chunks_ = 0, chunks_ = 0;
  uint16_t chunk_header_ = 0, chunk_type_ = 0;
  size_t need_ = 0;
  SHA256_CTX hash_;
  std::vector<uint8_t> buffer_;
  std::chrono::steady_clock::time_point last_progress_{};
  bool Output(const uint8_t* data, size_t bytes) {
    if (bytes > part_.bytes - written_) return false;
    SHA256_Update(&hash_, data, bytes);
    if (part_.sparse) output_crc_ = crc32(output_crc_, data, bytes);
    size_t offset = 0;
    while (fd_ >= 0 && offset < bytes) {
      const auto n = write(fd_, data + offset, bytes - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      offset += n;
    }
    written_ += bytes;
    const auto now = std::chrono::steady_clock::now();
    if (progress_ && (written_ == part_.bytes || now - last_progress_ >= std::chrono::milliseconds(100))) {
      progress_(written_);
      last_progress_ = now;
    }
    return true;
  }
  bool Repeat(uint64_t bytes, const uint8_t* fill = nullptr) {
    std::array<uint8_t, 65536> data{};
    if (fill) for (size_t i = 0; i < data.size(); i += 4) memcpy(data.data()+i, fill, 4);
    while (bytes) {
      const size_t n = std::min<uint64_t>(bytes, data.size());
      if (!Output(data.data(), n)) return false;
      bytes -= n;
    }
    return true;
  }
  bool BeginData() {
    buffer_.clear();
    switch (chunk_type_) {
      case 0xcac1: state_ = Data; remaining_ = chunk_bytes_; break;
      case 0xcac2: state_ = Fill; need_ = 4; break;
      case 0xcac3: if (!Repeat(chunk_bytes_)) return false; NextChunk(); break;
      case 0xcac4: state_ = Crc; need_ = 4; break;
      default: return false;
    }
    return true;
  }
  void NextChunk() { ++chunks_; state_ = Chunk; need_ = 12; buffer_.clear(); }
  bool Append(const uint8_t* data, size_t bytes) {
    encoded_ += bytes;
    encoded_crc_ = crc32(encoded_crc_, data, bytes);
    while (bytes) {
      if (state_ == Raw) return Output(data, bytes);
      if (state_ == Data) {
        const size_t n = std::min<uint64_t>(bytes, remaining_);
        if (!Output(data, n)) return false;
        data += n; bytes -= n; remaining_ -= n;
        if (!remaining_) NextChunk();
        continue;
      }
      if (state_ == SkipHeader || state_ == SkipChunk) {
        const size_t n = std::min<uint64_t>(bytes, remaining_);
        data += n; bytes -= n; remaining_ -= n;
        if (!remaining_) {
          if (state_ == SkipHeader) { state_ = Chunk; need_ = 12; }
          else if (!BeginData()) return false;
        }
        continue;
      }
      const size_t n = std::min(bytes, need_ - buffer_.size());
      buffer_.insert(buffer_.end(), data, data + n);
      data += n; bytes -= n;
      if (buffer_.size() != need_) continue;
      if (state_ == Header) {
        const auto* p = buffer_.data();
        if (Le32(p) != 0xed26ff3a || Le16(p+4) != 1 || Le16(p+8) < 28 || Le16(p+8) > 1024 ||
            Le16(p+10) < 12 || Le16(p+10) > 1024) return false;
        block_ = Le32(p+12); total_chunks_ = Le32(p+20); image_crc_ = Le32(p+24);
        if (!block_ || block_ % 4 || block_ > (1U<<20) || !total_chunks_ || total_chunks_ > 10000000 ||
            uint64_t(block_) * Le32(p+16) != part_.bytes) return false;
        chunk_header_ = Le16(p+10); remaining_ = Le16(p+8)-28;
        buffer_.clear(); state_ = remaining_ ? SkipHeader : Chunk; need_ = 12;
      } else if (state_ == Chunk) {
        if (chunks_ >= total_chunks_) return false;
        const auto* p = buffer_.data(); chunk_type_ = Le16(p);
        chunk_bytes_ = uint64_t(Le32(p+4)) * block_;
        if (chunk_bytes_ > part_.bytes - written_) return false;
        uint64_t payload;
        if (chunk_type_ == 0xcac1) payload = chunk_bytes_;
        else if (chunk_type_ == 0xcac2 || chunk_type_ == 0xcac4) payload = 4;
        else if (chunk_type_ == 0xcac3) payload = 0;
        else return false;
        if ((chunk_type_ == 0xcac4 ? chunk_bytes_ != 0 : chunk_bytes_ == 0) ||
            uint64_t(Le32(p+8)) != chunk_header_ + payload) return false;
        remaining_ = chunk_header_ - 12;
        buffer_.clear();
        if (remaining_) state_ = SkipChunk;
        else if (!BeginData()) return false;
      } else if (state_ == Fill) {
        if (!Repeat(chunk_bytes_, buffer_.data())) return false;
        NextChunk();
      } else if (state_ == Crc) {
        if (Le32(buffer_.data()) != output_crc_) return false;
        NextChunk();
      }
    }
    return true;
  }
};
} // namespace

Info InspectImageZip(ZipArchiveHandle archive, const std::string& path, bool read_arb) {
  Info info;
  ZipEntry64 android_info;
  if (FindEntry(archive, "android-info.txt", &android_info)) return info;
  info.is_image_zip = true;
  if (!Small(archive, "android-info.txt", info.image_zip_requirements) ||
      info.image_zip_requirements.find('\0') != std::string::npos) {
    info.error = "Invalid android-info.txt"; return info;
  }
  info.target_build = path.substr(path.find_last_of('/') + 1);
  info.name_from_filename = true;
  SHA256_CTX hash; SHA256_Init(&hash);
  Bind(hash, info.image_zip_requirements);
  std::string layout;
  ZipEntry64 layout_entry;
  if (!FindEntry(archive, "super_empty.img", &layout_entry)) {
    if (!Small(archive, "super_empty.img", layout, 4 * 1024 * 1024) || !ReadLayout(layout, info)) {
      info.error = "Unsupported or invalid super_empty.img layout metadata"; return info;
    }
    Bind(hash, layout);
  }
  info.dynamic_partitions = !info.image_zip_layout.empty();
  std::map<std::string, ZipEntry64> images;
  void* cookie = nullptr;
  if (StartIteration(archive, &cookie)) { info.error = "Cannot list ZIP images"; return info; }
  ZipEntry64 entry; std::string_view name;
  int result;
  while ((result = Next(cookie, &entry, &name)) == 0) {
    std::string file(name);
    if (file == "super_empty.img" || file.size() < 5 || file.substr(file.size()-4) != ".img") continue;
    const auto bare = file.substr(0, file.size()-4);
    if (!Name(bare) || (entry.gpbf & 1) || ((entry.unix_mode & S_IFMT) && !S_ISREG(entry.unix_mode)) ||
        !images.emplace(bare, entry).second || images.size() > 512) {
      info.error = "Unsafe, nested or duplicate image name in ZIP"; break;
    }
  }
  EndIteration(cookie);
  if (!info.error.empty()) return info;
  if (result != -1 || images.empty()) { info.error = "No readable partition images in ZIP"; return info; }
  for (const auto& [name, image] : images) {
    Partition part; part.name = name; part.bytes = image.uncompressed_length;
    Bind(hash, name);
    Bind(hash, std::to_string(image.offset) + ":" + std::to_string(image.method) + ":" +
        std::to_string(image.crc32) + ":" + std::to_string(image.compressed_length) + ":" +
        std::to_string(image.uncompressed_length));
    Prefix prefix;
    if (part.bytes && part.bytes <= kMaxImage && (image.method == 0 || image.method == 8)) {
      ProcessZipEntryContents(archive, &image, PrefixBytes, &prefix);
      if (prefix.bytes.size() >= 4 && Le32(prefix.bytes.data()) == 0xed26ff3a) {
        part.sparse = true;
        if (prefix.bytes.size() >= 28 && Le16(prefix.bytes.data()+4) == 1) {
          part.bytes = uint64_t(Le32(prefix.bytes.data()+12)) * Le32(prefix.bytes.data()+16);
        } else part.bytes = 0;
      }
      part.extractable = part.bytes && part.bytes <= kMaxImage && !prefix.bytes.empty();
    }
    if (!part.extractable) part.extraction_error = "Unsupported image size, encoding or compression";
    if (part.bytes > UINT64_MAX - info.expanded_bytes) { info.error = "Image sizes overflow"; return info; }
    info.expanded_bytes += part.bytes; info.payload_bytes += image.compressed_length;
    info.partitions.push_back(part);
  }
  unsigned char digest[SHA256_DIGEST_LENGTH]; SHA256_Final(digest, &hash);
  info.manifest_hash.assign(reinterpret_cast<char*>(digest), sizeof(digest));
  std::istringstream requirements(info.image_zip_requirements);
  std::string requirement;
  while (std::getline(requirements, requirement)) {
    const auto equal = requirement.find('=');
    if (equal == std::string::npos) continue;
    auto key = Trim(requirement.substr(0, equal));
    if (key == "require board" || key == "require product" || key == "board" || key == "product")
      info.target_device = Trim(requirement.substr(equal+1));
  }
  if (read_arb) {
    std::string bytes;
    if (Small(archive, "xbl_config.img", bytes, 16 * 1024 * 1024)) {
      const auto arb = ReadFirmwareArb(std::vector<uint8_t>(bytes.begin(), bytes.end()));
      info.arb_available = arb.available; info.arb_index = arb.index; info.arb_detail = arb.detail;
    }
  }
  info.valid = true;
  return info;
}

bool ImageZipCompatible(const Info& info, const std::map<std::string, std::vector<std::string>>& device,
                        std::string& error) {
  if (!info.is_image_zip || !info.valid) { error = "Invalid image archive"; return false; }
  std::istringstream stream(info.image_zip_requirements); std::string line;
  bool identity = false;
  while (std::getline(stream, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#') continue;
    bool reject = false;
    if (line.rfind("require ", 0) == 0) line = Trim(line.substr(8));
    else if (line.rfind("reject ", 0) == 0) { line = Trim(line.substr(7)); reject = true; }
    else if (line.rfind("require-for-product:", 0) == 0) {
      error = "Conditional android-info requirements are not supported"; return false;
    }
    const auto equal = line.find('=');
    if (equal == std::string::npos) { error = "Malformed android-info requirement"; return false; }
    auto key = Trim(line.substr(0, equal));
    if (key == "board") key = "product";
    const auto found = device.find(key);
    if (found == device.end() || found->second.empty()) { error = "Cannot verify package requirement: " + key; return false; }
    bool matched = false;
    std::istringstream alternatives(line.substr(equal+1)); std::string option;
    while (std::getline(alternatives, option, '|')) {
      option = Lower(Trim(option));
      if (option.empty()) { error = "Empty package requirement"; return false; }
      for (const auto& actual : found->second) if (Lower(Trim(actual)) == option) matched = true;
    }
    if (matched == reject) { error = "Package requirement failed: " + line; return false; }
    if (key == "product" && !reject) identity = true;
  }
  if (!identity) error = "Image ZIP has no verified board/product requirement";
  return identity;
}

bool ExtractZipImages(const std::string& path, const std::string& reviewed_hash,
    const std::vector<std::string>& names, const std::string& directory,
    std::vector<std::string>& hashes, std::string& error,
    const std::function<void(const std::string&, uint64_t, uint64_t)>& progress) {
  const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  struct stat before{};
  if (fd < 0) { error = "Cannot open image ZIP"; return false; }
  Archive archive;
  if (fstat(fd, &before) || !S_ISREG(before.st_mode) || OpenArchiveFd(fd, path.c_str(), &archive.handle, true)) {
    if (!archive.handle) close(fd);
    error = "Cannot open image ZIP"; return false;
  }
  const auto info = InspectImageZip(archive.handle, path, false);
  if (!info.valid || reviewed_hash.size() != 32 || info.manifest_hash != reviewed_hash || names.empty()) {
    error = "Image ZIP changed; review it again"; return false;
  }
  uint64_t total = 0, done = 0;
  std::set<std::string> unique;
  for (const auto& name : names) {
    auto part = std::find_if(info.partitions.begin(), info.partitions.end(), [&](const auto& p) { return p.name == name; });
    if (part == info.partitions.end() || !part->extractable || !unique.insert(name).second) {
      error = "Invalid selected image: " + name; return false;
    }
    total += part->bytes;
  }
  struct statvfs space{};
  if (statvfs(directory.c_str(), &space) || total > uint64_t(space.f_bavail) * space.f_frsize) {
    error = "Not enough storage to stage and verify the selected images"; return false;
  }
  const int dirfd = open(directory.c_str(), O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (dirfd < 0) { error = "Invalid extraction directory"; return false; }
  std::vector<std::string> created;
  bool ok = true;
  hashes.clear();
  for (const auto& name : names) {
    const auto part = std::find_if(info.partitions.begin(), info.partitions.end(), [&](const auto& p) { return p.name == name; });
    const auto filename = name + ".img";
    const int output = openat(dirfd, filename.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (output < 0) { ok = false; break; }
    created.push_back(filename);
    ZipEntry64 entry;
    ImageWriter writer(output, *part, [&](uint64_t bytes) { if (progress) progress("Extracting " + name, done + bytes, total); });
    std::string digest;
    ok = !FindEntry(archive.handle, filename, &entry) &&
        !ProcessZipEntryContents(archive.handle, &entry, ImageWriter::Append, &writer) && writer.Finish(entry, digest);
    if (ok) ok = !fsync(output);
    close(output);
    if (!ok) break;
    hashes.push_back(digest); done += part->bytes;
  }
  struct stat after{};
  ok = ok && !fstat(fd, &after) && before.st_size == after.st_size &&
      before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
      before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
  if (!ok) {
    for (const auto& file : created) unlinkat(dirfd, file.c_str(), 0);
    hashes.clear(); error = "Image ZIP extraction, CRC, sparse validation or storage write failed; no partitions written";
  }
  close(dirfd);
  return ok;
}
} // namespace aeraui::payload
