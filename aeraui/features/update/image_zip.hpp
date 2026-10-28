/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "payload_inspector.hpp"
#include <map>
#include <ziparchive/zip_archive.h>
namespace aeraui::payload {
// android-info image archives only; does not execute fastboot-info commands.
Info InspectImageZip(ZipArchiveHandle archive, const std::string& path, bool read_arb);
bool ImageZipCompatible(const Info& info, const std::map<std::string, std::vector<std::string>>& device,
                        std::string& error);
// Produces raw images (including deterministic sparse zero extents). All ZIP
// CRCs and sparse checksums are checked before returning any files to a writer.
bool ExtractZipImages(const std::string& path, const std::string& reviewed_hash,
    const std::vector<std::string>& names, const std::string& directory,
    std::vector<std::string>& hashes, std::string& error,
    const std::function<void(const std::string&, uint64_t, uint64_t)>& progress = {});
}
