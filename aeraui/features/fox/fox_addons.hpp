/*
 * Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>
#include <vector>

namespace aeraui::fox {

// Why an add-on is disabled before the user explicitly overrides the guard.
enum class Risk {
  kNone,
  kConflict,  // ASRP already implements what this add-on does.
  kUnsafe,    // Low-level commands may crash ASRP.
};

// A legacy OrangeFox add-on package. Both the built-in add-ons that ship in
// /FFiles and packages dropped into /sdcard/AERA/plugins/fox are ordinary
// flashable ZIP archives, so they can run through the standard ZIP installer.
struct Addon {
  std::string id;  // Archive path, used as the stable identity.
  std::string name;
  std::string version;
  std::string author;
  std::string description;
  std::string path;
  bool builtin = false;
  Risk risk = Risk::kNone;
  std::string reason;    // Catalog key explaining why the add-on is disabled.
  std::string evidence;  // Optional raw detail, such as a detected command.
};

// Scans the OrangeFox locations that are present in this recovery image:
//   /FFiles                     built-in add-ons from the recovery ramdisk
//   /sdcard/Fox/FoxFiles        Magisk and init.d helpers
//   /sdcard/AERA/plugins/fox    user supplied legacy packages
// Missing directories are skipped. The result is sorted with built-in add-ons
// first, then user packages by name.
std::vector<Addon> Scan();

}  // namespace aeraui::fox
