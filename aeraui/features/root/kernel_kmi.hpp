/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <regex.h>
#include <string>
#include <vector>

namespace aeraui::root {
inline std::vector<std::string> KernelReleasesFromBanner(const std::string &bytes) {
  std::vector<std::string> releases;
  size_t offset = 0;
  while ((offset = bytes.find("Linux version ", offset)) != std::string::npos) {
    const size_t start = offset + 14;
    offset = start;
    size_t end = start;
    while (end < bytes.size() && end - start < 200 &&
           bytes[end] != '\0' && bytes[end] != ' ' && bytes[end] != '\t' && bytes[end] != '\r' && bytes[end] != '\n') ++end;
    if (end == bytes.size()) continue;  // Wait for the next chunk's complete release.
    releases.push_back(bytes.substr(start, end - start));
  }
  return releases;
}

inline std::string KernelKmiFromRelease(const std::string &release) {
  regex_t pattern{};
  if (regcomp(&pattern, "^([0-9]+\\.[0-9]+)\\.[0-9]+[-[:alnum:]_.+~]*-(android[0-9]+)([-[:alnum:]_.+~]*)$", REG_EXTENDED) != 0)
    return {};
  regmatch_t matches[4]{};
  const bool matched = regexec(&pattern, release.c_str(), 4, matches, 0) == 0;
  regfree(&pattern);
  if (!matched) return {};
  return release.substr(matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so) + "-" +
      release.substr(matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
}

inline std::string KernelVersionFromRelease(const std::string &release) {
  regex_t pattern{};
  if (regcomp(&pattern, "^([0-9]+\\.[0-9]+)\\.[0-9]+[-[:alnum:]_.+~]*$", REG_EXTENDED) != 0)
    return {};
  regmatch_t matches[2]{};
  const bool matched = regexec(&pattern, release.c_str(), 2, matches, 0) == 0;
  regfree(&pattern);
  if (!matched) return {};
  return release.substr(matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
}

inline std::string KernelVersionFromKmi(const std::string &kmi) {
  regex_t pattern{};
  if (regcomp(&pattern, "^android[0-9]+-([0-9]+\\.[0-9]+)$", REG_EXTENDED) != 0)
    return {};
  regmatch_t matches[2]{};
  const bool matched = regexec(&pattern, kmi.c_str(), 2, matches, 0) == 0;
  regfree(&pattern);
  if (!matched) return {};
  return kmi.substr(matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
}

inline std::string ResolveKernelKmiFromRelease(
    const std::string &release, const std::vector<std::string> &supported_kmis) {
  const std::string exact = KernelKmiFromRelease(release);
  if (!exact.empty()) return exact;

  // Never reinterpret a banner that advertises an Android release but failed exact parsing.
  // The fallback is only for kernels whose release string stripped the Android KMI portion.
  if (release.find("android") != std::string::npos) return {};
  const std::string version = KernelVersionFromRelease(release);
  if (version.empty()) return {};

  std::string candidate;
  for (const std::string &kmi : supported_kmis) {
    if (KernelVersionFromKmi(kmi) != version) continue;
    if (candidate.empty() || candidate == kmi) {
      candidate = kmi;
      continue;
    }
    return {};  // Multiple Android releases share this kernel version.
  }
  return candidate;
}

inline std::string KernelKmiFromBanner(const std::string &bytes) {
  for (const std::string &release : KernelReleasesFromBanner(bytes)) {
    const std::string kmi = KernelKmiFromRelease(release);
    if (!kmi.empty()) return kmi;
  }
  return {};
}

inline std::string ResolveKernelKmiFromBanner(
    const std::string &bytes, const std::vector<std::string> &supported_kmis) {
  for (const std::string &release : KernelReleasesFromBanner(bytes)) {
    const std::string kmi = ResolveKernelKmiFromRelease(release, supported_kmis);
    if (!kmi.empty()) return kmi;
  }
  return {};
}
}  // namespace aeraui::root
