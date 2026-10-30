/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "protocol.hpp"
#include "clipboard.hpp"
#include <string>

namespace aeraui::plugin_api {
inline uint32_t InputCharacters(const std::string &text) {
  uint32_t count = 0;
  for (unsigned char c : text) if ((c & 0xc0) != 0x80) ++count;
  return count;
}
inline bool InputFlagsValid(uint32_t flags) {
  constexpr uint32_t mask = kInputPassword | kInputNumber | kInputMultiline | kInputDisabled;
  return !(flags & ~mask) &&
      (!(flags & kInputMultiline) || !(flags & (kInputPassword | kInputNumber)));
}
inline bool InputTextValid(const std::string &text, uint32_t flags, uint32_t maximum) {
  if (!maximum || maximum > kMaxInputCharacters || text.size() > 1023 ||
      !ClipboardUtf8(text) || InputCharacters(text) > maximum) return false;
  for (unsigned char c : text) {
    if (c < 0x20 && !(c == '\n' && (flags & kInputMultiline))) return false;
    if (c == 0x7f) return false;
    if ((flags & kInputNumber) && (c < '0' || c > '9') && c != '.' && c != '-') return false;
  }
  return true;
}
}  // namespace aeraui::plugin_api
