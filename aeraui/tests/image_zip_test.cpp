/* SPDX-License-Identifier: Apache-2.0 */
#include "features/update/image_zip.hpp"
#include "../../aera_image_flash_layout.hpp"
#include <android-base/file.h>
#include <gtest/gtest.h>
#include <liblp/liblp.h>
#include <openssl/sha.h>
#include <ziparchive/zip_writer.h>
#include <zlib.h>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

using namespace aeraui::payload;
namespace {
struct ImageZip {
  TemporaryFile file;
  void Make(const std::vector<std::pair<std::string, std::string>>& entries, bool compress = false) {
    FILE* stream = fdopen(dup(file.fd), "wb");
    ASSERT_NE(stream, nullptr);
    {
      ZipWriter zip(stream);
      for (const auto& [name, data] : entries) {
        ASSERT_EQ(zip.StartEntry(name.c_str(), compress ? ZipWriter::kCompress : 0), 0);
        ASSERT_EQ(zip.WriteBytes(data.data(), data.size()), 0);
        ASSERT_EQ(zip.FinishEntry(), 0);
      }
      ASSERT_EQ(zip.Finish(), 0);
    }
    fclose(stream);
  }
};
std::string Digest(const std::string& data) {
  unsigned char digest[32]; SHA256(reinterpret_cast<const uint8_t*>(data.data()), data.size(), digest);
  return {reinterpret_cast<char*>(digest), sizeof(digest)};
}
void U32(std::string& data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) data.push_back(value >> (i*8));
}
void U16(std::string& data, uint16_t value) { data.push_back(value); data.push_back(value >> 8); }
std::string Sparse() {
  std::string data;
  U32(data, 0xed26ff3a); U16(data, 1); U16(data, 0); U16(data, 28); U16(data, 12);
  U32(data, 4096); U32(data, 3); U32(data, 3); U32(data, 0);
  U16(data, 0xcac1); U16(data, 0); U32(data, 1); U32(data, 4108); data += std::string(4096, 'r');
  U16(data, 0xcac2); U16(data, 0); U32(data, 1); U32(data, 16); data += "fill";
  U16(data, 0xcac3); U16(data, 0); U32(data, 1); U32(data, 12);
  return data;
}
std::string LayoutBlob() {
  auto builder = android::fs_mgr::MetadataBuilder::New(64 * 1024 * 1024, 65536, 2);
  for (const auto* suffix : {"_a", "_b"}) {
    builder->AddGroup(std::string("rom") + suffix, 24 * 1024 * 1024);
    builder->AddPartition(std::string("system") + suffix, std::string("rom") + suffix, LP_PARTITION_ATTR_READONLY);
  }
  TemporaryFile file;
  EXPECT_TRUE(android::fs_mgr::WriteToImageFile(file.path, *builder->Export()));
  std::string data; EXPECT_TRUE(android::base::ReadFileToString(file.path, &data));
  return data;
}
}
TEST(ImageZip, DetectsImagesAndKeepsEmptySuperAsMetadataOnly) {
  ImageZip zip;
  zip.Make({{"android-info.txt", "require board=sun|OP5D55L1\n"}, {"system.img", std::string(4096, 's')},
            {"super_empty.img", LayoutBlob()}, {"fastboot-info.txt", "if-wipe erase userdata\n"}}, true);
  const auto info = InspectZip(zip.file.path);
  ASSERT_TRUE(info.valid) << info.error;
  EXPECT_TRUE(info.is_image_zip); EXPECT_FALSE(info.is_payload);
  ASSERT_EQ(info.partitions.size(), 1U); EXPECT_EQ(info.partitions[0].name, "system");
  ASSERT_EQ(info.image_zip_layout.size(), 1U); EXPECT_EQ(info.image_zip_layout[0].name, "system");
  EXPECT_EQ(info.image_zip_groups[0].name, "rom");
  EXPECT_EQ(info.target_device, "sun|OP5D55L1");
}
TEST(ImageZip, OrdinaryInstallerZipRetainsItsExistingPath) {
  ImageZip zip;
  zip.Make({{"META-INF/com/google/android/update-binary", "installer"}, {"boot.img", "image"}});
  const auto info = InspectZip(zip.file.path);
  EXPECT_FALSE(info.is_payload);
  EXPECT_FALSE(info.is_image_zip);
  EXPECT_TRUE(info.error.empty());
}
TEST(ImageZip, RejectsWrongBoardUnknownRequirementsAndNoIdentity) {
  Info info; info.is_image_zip = info.valid = true;
  info.image_zip_requirements = "require board=sun|OP5D55L1\n";
  std::string error;
  EXPECT_TRUE(ImageZipCompatible(info, {{"product", {"SUN"}}}, error));
  EXPECT_FALSE(ImageZipCompatible(info, {{"product", {"CANOE", "lighthouse"}}}, error));
  info.image_zip_requirements += "require unsupported=foo\n";
  EXPECT_FALSE(ImageZipCompatible(info, {{"product", {"SUN"}}}, error));
  info.image_zip_requirements = "reject board=canoe\n";
  EXPECT_FALSE(ImageZipCompatible(info, {{"product", {"SUN"}}}, error));
}
TEST(ImageZip, ExtractsAndHashesStoredAndDeflatedImages) {
  for (bool compressed : {false, true}) {
    ImageZip zip; const std::string data(131073, 'v');
    zip.Make({{"android-info.txt", "require board=sun\n"}, {"vendor.img", data}}, compressed);
    const auto info = InspectZip(zip.file.path); ASSERT_TRUE(info.valid) << info.error;
    TemporaryDir dir; std::string error; std::vector<std::string> hashes;
    ASSERT_TRUE(ExtractZipImages(zip.file.path, info.manifest_hash, {"vendor"}, dir.path, hashes, error)) << error;
    ASSERT_EQ(hashes.size(), 1U); EXPECT_EQ(hashes[0], Digest(data));
    std::string extracted; ASSERT_TRUE(android::base::ReadFileToString(std::string(dir.path)+"/vendor.img", &extracted));
    EXPECT_EQ(extracted, data);
    EXPECT_FALSE(ExtractZipImages(zip.file.path, info.manifest_hash, {"vendor"}, dir.path, hashes, error)); // No overwrite.
  }
}
TEST(ImageZip, ExpandsSparseRawFillAndDiscardDeterministically) {
  ImageZip zip; zip.Make({{"android-info.txt", "require board=sun\n"}, {"system.img", Sparse()}}, true);
  const auto info = InspectZip(zip.file.path); ASSERT_TRUE(info.valid) << info.error;
  ASSERT_EQ(info.partitions.size(), 1U); EXPECT_TRUE(info.partitions[0].sparse);
  EXPECT_EQ(info.partitions[0].bytes, 12288U);
  TemporaryDir dir; std::string error; std::vector<std::string> hashes;
  ASSERT_TRUE(ExtractZipImages(zip.file.path, info.manifest_hash, {"system"}, dir.path, hashes, error)) << error;
  std::string expected(4096, 'r'); for (int i = 0; i < 1024; ++i) expected += "fill"; expected += std::string(4096, 0);
  EXPECT_EQ(hashes[0], Digest(expected));
}
TEST(ImageZip, RejectsBadCrcAndDeletesEveryCreatedImage) {
  ImageZip zip; zip.Make({{"android-info.txt", "require board=sun\n"}, {"boot.img", std::string(4096, 'b')},
                         {"vendor.img", std::string(4096, 'v')}});
  const auto info = InspectZip(zip.file.path); ASSERT_TRUE(info.valid) << info.error;
  // Damage image content without changing its directory/metadata review token.
  std::string bytes; ASSERT_TRUE(android::base::ReadFileToString(zip.file.path, &bytes));
  auto where = bytes.find(std::string(100, 'v')); ASSERT_NE(where, std::string::npos);
  bytes[where] = '!'; ASSERT_TRUE(android::base::WriteStringToFile(bytes, zip.file.path));
  TemporaryDir dir; std::string error; std::vector<std::string> hashes;
  EXPECT_FALSE(ExtractZipImages(zip.file.path, info.manifest_hash, {"boot", "vendor"}, dir.path, hashes, error));
  EXPECT_EQ(access((std::string(dir.path)+"/boot.img").c_str(), F_OK), -1);
  EXPECT_EQ(access((std::string(dir.path)+"/vendor.img").c_str(), F_OK), -1);
}
TEST(ImageZip, RejectsMalformedSparseAndChangedOrDuplicateSelections) {
  auto data = Sparse(); data.pop_back();
  ImageZip zip; zip.Make({{"android-info.txt", "require board=sun\n"}, {"system.img", data}});
  const auto info = InspectZip(zip.file.path); ASSERT_TRUE(info.valid);
  TemporaryDir dir; std::string error; std::vector<std::string> hashes;
  EXPECT_FALSE(ExtractZipImages(zip.file.path, info.manifest_hash, {"system"}, dir.path, hashes, error));
  EXPECT_FALSE(ExtractZipImages(zip.file.path, std::string(32, 'x'), {"system"}, dir.path, hashes, error));
  EXPECT_FALSE(ExtractZipImages(zip.file.path, info.manifest_hash, {"system", "system"}, dir.path, hashes, error));
  EXPECT_FALSE(ExtractZipImages(zip.file.path, info.manifest_hash, {"super_empty"}, dir.path, hashes, error));
}
TEST(ImageZip, RejectsUnsafeNestedImageAndMalformedTemplate) {
  ImageZip zip; zip.Make({{"android-info.txt", "require board=sun\n"}, {"../boot.img", "bad"}});
  EXPECT_FALSE(InspectZip(zip.file.path).valid);
  ImageZip template_zip; template_zip.Make({{"android-info.txt", "require board=sun\n"}, {"boot.img", "good"}, {"super_empty.img", "bad"}});
  EXPECT_FALSE(InspectZip(template_zip.file.path).valid);
}
TEST(ImageZipLayout, ReplacesOnlyCurrentSlotAndRequiresEveryRomLogicalImage) {
  constexpr uint64_t mb = 1024 * 1024;
  auto original = android::fs_mgr::MetadataBuilder::New(64 * mb, 65536, 2);
  ASSERT_TRUE(original->AddGroup("stock_a", 24 * mb));
  ASSERT_TRUE(original->AddGroup("stock_b", 24 * mb));
  auto* other = original->AddPartition("stock_only_b", "stock_b", 0);
  ASSERT_TRUE(original->ResizePartition(other, 4 * mb));
  auto* old = original->AddPartition("stock_only_a", "stock_a", 0);
  ASSERT_TRUE(original->ResizePartition(old, 4 * mb));
  aera::RomImageLayout layout{{{"rom", 24 * mb}}, {{"system", "rom", LP_PARTITION_ATTR_READONLY}}};
  std::string error;
  auto plan = android::fs_mgr::MetadataBuilder::New(*original->Export());
  EXPECT_FALSE(aera::PlanRomImageLayout(*plan, layout, "_a", {}, error));
  ASSERT_TRUE(aera::PlanRomImageLayout(*plan, layout, "_a", {{"system_a", 8 * mb}}, error)) << error;
  ASSERT_TRUE(aera::PlanImageFlashLayout(*plan, {{"system_a", 8 * mb}}, {}, error)) << error;
  EXPECT_EQ(plan->FindPartition("stock_only_a"), nullptr);
  EXPECT_EQ(plan->FindPartition("stock_only_b")->size(), 4 * mb);
  EXPECT_EQ(plan->FindPartition("system_a")->size(), 8 * mb);
  EXPECT_NE(plan->Export(), nullptr);
  // Planning never mutates the original checkpoint or its extents.
  EXPECT_NE(original->FindPartition("stock_only_a"), nullptr);
  EXPECT_EQ(original->FindPartition("system_a"), nullptr);
}
TEST(ImageZip, OptionalRealPackageInspection) {
  const char* path = getenv("AERA_IMAGE_ZIP_TEST_FILE");
  if (!path) GTEST_SKIP();
  const auto info = InspectZip(path, true);
  ASSERT_TRUE(info.valid) << info.error;
  ASSERT_TRUE(info.is_image_zip);
  ASSERT_FALSE(info.image_zip_layout.empty());
  EXPECT_EQ(std::count_if(info.partitions.begin(), info.partitions.end(), [](const auto& p) { return p.name == "super_empty"; }), 0);
  std::string error;
  EXPECT_TRUE(ImageZipCompatible(info, {{"product", {"SUN"}}}, error)) << error;
  EXPECT_FALSE(ImageZipCompatible(info, {{"product", {"CANOE", "lighthouse"}}}, error));
  TemporaryDir dir;
  std::vector<std::string> hashes;
  EXPECT_TRUE(ExtractZipImages(path, info.manifest_hash, {"vbmeta", "xbl_config"}, dir.path, hashes, error)) << error;
  EXPECT_EQ(hashes.size(), 2U);
}
