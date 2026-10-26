/* Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0 */
#include "aeraui/theme_preset.hpp"
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

int main() {
  std::set<std::string> names;
  for (const auto &preset : aeraui::kThemePresets) {
    assert(aeraui::ValidThemePreset(preset));
    assert(names.emplace(preset.name).second);
  }
  assert(!aeraui::kThemePresets.empty());
  assert(names.count("OrangeFox") == 1u);
  assert(names.count("YARP") == 1u);
  // Upstream accents: OrangeFox accent.xml line 18, YARP ui_metrics.cpp line 87.
  assert(aeraui::kThemePresets[0].accent == 0xed6f02);
  assert(aeraui::kThemePresets[1].accent == 0x347ff1);
  auto invalid = aeraui::kThemePresets[0];
  invalid.accent = 0x01000000u;
  assert(!aeraui::ValidThemePreset(invalid));
  invalid = aeraui::kThemePresets[0];
  invalid.name = nullptr;
  assert(!aeraui::ValidThemePreset(invalid));
  puts("Theme pack names, colours and enum ranges are valid.");
}
