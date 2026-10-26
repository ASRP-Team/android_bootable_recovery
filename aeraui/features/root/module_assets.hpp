/* Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "root_manager.hpp"

namespace aeraui::root {
inline std::vector<std::string> ModuleAssetNames(Provider provider,
                                                const std::string &kmi) {
  switch (provider) {
    case Provider::kKernelSU:
      return {"lkm-aarch64-" + kmi + "_kernelsu.ko"};
    case Provider::kKernelSUNext:
      return {"aarch64_" + kmi + "_kernelsu.ko",
              "aarch64-" + kmi + "_kernelsu.ko",
              kmi + "_kernelsu.ko"};
    case Provider::kSukiSU:
      return {"lkm-aarch64-" + kmi + "_kernelsu.ko",
              "aarch64-" + kmi + "-lkm.zip"};
    case Provider::kBakaSU:
      return {"lkm-aarch64-" + kmi + "_kernelsu.ko",
              "aarch64_" + kmi + "_kernelsu.ko",
              kmi + "_kernelsu.ko",
              "aarch64-" + kmi + "-lkm.zip"};
    case Provider::kFinaSU:
      return {"lkm-aarch64-" + kmi + "_kernelsu.ko",
              "aarch64_" + kmi + "_kernelsu.ko",
              kmi + "_kernelsu.ko",
              "finasu-aarch64-" + kmi + ".ko",
              "aarch64-" + kmi + "-lkm.zip"};
    case Provider::kYukiSU:
      return {kmi + "_kernelsu.ko"};
    // KernelPatch providers are patched with kptools + kpimg instead of a
    // KernelSU LKM, so there is no downloadable kernel module asset.
    case Provider::kAPatch:
    case Provider::kFolkPatch:
      return {};
  }
  return {};
}

inline bool ModuleAssetArchive(Provider provider, const std::string &name) {
  constexpr char suffix[] = "-lkm.zip";
  return provider == Provider::kSukiSU && name.size() >= sizeof(suffix) - 1 &&
      name.compare(name.size() - (sizeof(suffix) - 1), sizeof(suffix) - 1, suffix) == 0;
}
}  // namespace aeraui::root
