/*
 * Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <cstdint>

#include "aeraui/backend.hpp"

namespace aeraui {

// A preset now describes a whole recovery look, not a single accent swatch.
// Every field is applied through the existing preference setters, so no preset
// needs a private storage key of its own.
enum class ThemePresetWallpaper { kKeep, kCanvas };

struct ThemePreset {
  const char *name;
  uint32_t accent;
  uint32_t accent_secondary; // Kept for cards/details; the runtime has one accent.
  AppearanceMode appearance;
  SurfaceStyle surface;
  InterfaceSize interface_size;
  ThemePresetWallpaper wallpaper;
};

// Upstream values: OrangeFox R12 accent.xml and styles/Dark.xml,
// YARP gui2/core/ui_metrics.cpp dark palette. Append new packs here.
inline constexpr std::array<ThemePreset, 2> kThemePresets{{
    {"OrangeFox", 0xed6f02, 0xd86100, AppearanceMode::kGraphite,
     SurfaceStyle::kSolid, InterfaceSize::kNormal, ThemePresetWallpaper::kCanvas},
    {"YARP", 0x347ff1, 0x9bc5e9, AppearanceMode::kAmoled,
     SurfaceStyle::kSolid, InterfaceSize::kNormal, ThemePresetWallpaper::kCanvas},
}};

inline bool ValidThemePreset(const ThemePreset &preset) {
  const auto valid_color = [](uint32_t rgb) {
    return rgb != 0 && rgb <= 0x00ffffffu;
  };
  const int appearance = static_cast<int>(preset.appearance);
  const int surface = static_cast<int>(preset.surface);
  const int interface_size = static_cast<int>(preset.interface_size);
  return preset.name != nullptr && *preset.name != '\0' &&
      valid_color(preset.accent) &&
      (preset.accent_secondary == 0 || valid_color(preset.accent_secondary)) &&
      appearance >= 0 && appearance <= 2 &&
      surface >= 0 && surface <= 3 &&
      interface_size >= 0 && interface_size <= 3 &&
      (preset.wallpaper == ThemePresetWallpaper::kKeep ||
       preset.wallpaper == ThemePresetWallpaper::kCanvas);
}

} // namespace aeraui
