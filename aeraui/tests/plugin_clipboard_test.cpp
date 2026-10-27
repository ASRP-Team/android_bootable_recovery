/* SPDX-License-Identifier: Apache-2.0 */
#include "../features/plugin_api/clipboard.hpp"
#include <cassert>
#include <cstdio>

using namespace aeraui::plugin_api;
int main() {
  ClipboardTransfer channel;
  std::string shared = "original";
  unsigned writes = 0;
  auto run = [&](Message request, bool allowed = true) {
    return channel.Handle(request, allowed, [&]() -> const std::string & { return shared; },
        [&](const std::string &text) { shared = text; ++writes; });
  };
  Message request; request.kind = Kind::kGetClipboard; request.request_id = 1;
  assert(run(request, false).flags == kClipboardError);
  auto reply = run(request);
  assert(reply.flags == kClipboardComplete && std::string(reply.text) == shared);
  auto write = [&](const std::string &text) {
    request = {}; request.kind = Kind::kSetClipboard; request.request_id = 2;
    request.value = static_cast<uint32_t>(text.size());
    const auto before = shared;
    auto result = run(request);
    if (text.empty()) { assert(result.flags == kClipboardComplete); return; }
    assert(result.flags == kClipboardReady && shared == before);
    for (size_t off = 0; off < text.size();) {
      request = {}; request.kind = Kind::kClipboardChunk; request.request_id = 2;
      request.value = off;
      const size_t count = std::min(text.size() - off, sizeof(request.text) - 1);
      memcpy(request.text, text.data() + off, count); off += count;
      result = run(request);
      assert(result.value == off);
      assert(result.flags == (off == text.size() ? kClipboardComplete : kClipboardReady));
      if (off != text.size()) assert(shared == before);
    }
    assert(shared == text);
  };
  const std::string unicode = std::string(1022, 'a') + "\xd0\x9f\xe4\xbd\xa0\xf0\x9f\x98\x80\nsecond line";
  write(unicode);
  request = {}; request.kind = Kind::kGetClipboard; request.request_id = 3;
  reply = run(request); assert(reply.flags == kClipboardMore && reply.value == unicode.size());
  std::string received = reply.text;
  shared = "native clipboard changed";
  while (reply.flags == kClipboardMore) {
    request.flags = kClipboardContinue; request.value = received.size();
    reply = run(request); received += reply.text;
  }
  assert(received == unicode && shared == "native clipboard changed");
  write(std::string(kMaxClipboardBytes, 'z'));
  write(""); assert(shared.empty());
  shared = "preserve on failure";
  request = {}; request.kind = Kind::kSetClipboard; request.request_id = 4;
  request.value = kMaxClipboardBytes + 1;
  assert(run(request).flags == kClipboardError && shared == "preserve on failure");
  for (const std::string &invalid : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
      std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82"), std::string("\x80")}) {
    request = {}; request.kind = Kind::kSetClipboard; request.request_id = 5;
    request.value = invalid.size(); assert(run(request).flags == kClipboardReady);
    request.kind = Kind::kClipboardChunk; request.value = 0;
    memcpy(request.text, invalid.data(), invalid.size());
    assert(run(request).flags == kClipboardError && shared == "preserve on failure");
  }
  assert(!ClipboardUtf8(std::string("a\0b", 3)));
  request = {}; request.kind = Kind::kSetClipboard; request.request_id = 6; request.value = 8;
  assert(run(request).flags == kClipboardReady);
  request.kind = Kind::kClipboardChunk; request.value = 1; strcpy(request.text, "part");
  assert(run(request).flags == kClipboardError && shared == "preserve on failure");
  request.value = 0; assert(run(request).flags == kClipboardError);
  request = {}; request.kind = Kind::kSetClipboard; request.request_id = 7; request.value = 8;
  assert(run(request).flags == kClipboardReady);
  request.kind = Kind::kClipboardChunk; request.request_id = 8; request.value = 0;
  strcpy(request.text, "part"); assert(run(request).flags == kClipboardError);
  request = {}; request.kind = Kind::kSetClipboard; request.request_id = 9; request.value = 1;
  assert(run(request).flags == kClipboardReady);
  request.kind = Kind::kClipboardChunk; request.value = 0; strcpy(request.text, "too long");
  assert(run(request).flags == kClipboardError);
  request = {}; request.kind = Kind::kSetClipboard; request.request_id = 10; request.value = 4;
  assert(run(request).flags == kClipboardReady); channel.Reset();
  request.kind = Kind::kClipboardChunk; request.value = 0; strcpy(request.text, "part");
  assert(run(request).flags == kClipboardError && shared == "preserve on failure");
  request = {}; request.kind = Kind::kGetClipboard;
  assert(run(request).flags == kClipboardError);
  request.request_id = 11; request.flags = kClipboardContinue;
  assert(run(request).flags == kClipboardError);
  shared.assign(kMaxClipboardBytes + 1, 'x'); request.flags = 0;
  assert(run(request).flags == kClipboardError && shared.size() > kMaxClipboardBytes);
  assert(writes == 3);
  puts("Clipboard atomic writes, snapshots, UTF-8, limits and malformed transfers passed");
}
