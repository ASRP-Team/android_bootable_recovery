/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_api/text_input.hpp"
#include <cassert>
using namespace aeraui::plugin_api;
int main() {
  static_assert(sizeof(Message) == 1144);
  static_assert(static_cast<unsigned>(Kind::kGetClipboard) == 22);
  static_assert(static_cast<unsigned>(Kind::kClipboardResult) == 73);
  assert(InputTextValid("Gr??e / ?????? / ??", 0, 64));
  assert(!InputTextValid("first\nsecond", 0, 64));
  assert(InputTextValid("first\nsecond", kInputMultiline, 64));
  assert(InputTextValid("-12.5", kInputNumber, 5));
  assert(!InputTextValid("123abc", kInputNumber, 64));
  assert(!InputTextValid(std::string("a\0b",3), 0, 64));
  assert(!InputTextValid("\xc0\x80", 0, 64));
  assert(!InputFlagsValid(kInputMultiline | kInputPassword));
  assert(!InputFlagsValid(kInputMultiline | kInputNumber));
  assert(!InputFlagsValid(1U << 16));
  assert(InputFlagsValid(kInputPassword | kInputNumber));
  std::string emoji;
  for (unsigned i=0;i<255;++i) emoji += "\xf0\x9f\x98\x80";
  assert(InputTextValid(emoji, 0, 255));
  assert(!InputTextValid(emoji+"x", 0, 255));
  assert(!InputTextValid("", 0, 256));
  assert(InputCharacters("????") == 4);
}
