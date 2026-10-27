/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace aeraui::plugin_api {

inline bool ClipboardUtf8(const std::string &text) {
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = text[i++];
    if (!lead) return false;
    if (lead < 0x80) continue;
    const unsigned length = lead >= 0xc2 && lead <= 0xdf ? 2 :
        lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
    if (!length || text.size() - i < length - 1) return false;
    const unsigned char second = text[i];
    if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
        (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90)) return false;
    for (unsigned n = 1; n < length; ++n)
      if ((static_cast<unsigned char>(text[i++]) & 0xc0) != 0x80) return false;
  }
  return true;
}

// One acknowledged packet at a time avoids filling the nonblocking UI socket.
// Writes replace the shared clipboard only after all bytes pass validation.
class ClipboardTransfer {
 public:
  void Reset() {
    read_id_ = write_id_ = expected_ = read_offset_ = 0;
    snapshot_.clear(); pending_.clear();
  }

  template <typename Read, typename Write>
  Message Handle(const Message &request, bool allowed, Read read, Write write) {
    Message reply;
    reply.kind = Kind::kClipboardResult;
    reply.request_id = request.request_id;
    auto error = [&](const char *reason) {
      Reset(); reply.value = 0; reply.flags = kClipboardError;
      snprintf(reply.title, sizeof(reply.title), "%s", reason);
      return reply;
    };
    if (!allowed) return error("Clipboard is available only to the active plugin view.");
    if (!request.request_id || request.title[0] || !StringsValid(request))
      return error("Invalid clipboard request.");
    if (request.kind == Kind::kGetClipboard) {
      if (request.text[0] || (request.flags != 0 && request.flags != kClipboardContinue))
        return error("Invalid clipboard read.");
      if (!request.flags) {
        if (request.value) return error("Clipboard reads must start at offset zero.");
        Reset();
        const auto &text = read();
        if (text.size() > kMaxClipboardBytes || !ClipboardUtf8(text))
          return error("Clipboard text exceeds the limit or is not valid UTF-8.");
        snapshot_ = text; read_id_ = request.request_id;
      } else if (read_id_ != request.request_id || request.value != read_offset_) {
        return error("Clipboard read offset or request ID changed.");
      }
      const size_t count = std::min(snapshot_.size() - request.value, sizeof(reply.text) - 1);
      memcpy(reply.text, snapshot_.data() + request.value, count);
      reply.value = static_cast<uint32_t>(snapshot_.size());
      read_offset_ = request.value + count;
      reply.flags = read_offset_ < snapshot_.size() ? kClipboardMore : kClipboardComplete;
      if (reply.flags == kClipboardComplete) Reset();
      return reply;
    }
    if (request.kind == Kind::kSetClipboard) {
      if (request.flags || request.text[0] || request.value > kMaxClipboardBytes)
        return error("Invalid clipboard size or write request.");
      Reset(); expected_ = request.value; write_id_ = request.request_id;
      pending_.reserve(expected_);
      if (expected_) { reply.flags = kClipboardReady; return reply; }
      write(pending_); Reset(); reply.flags = kClipboardComplete; return reply;
    }
    if (request.kind != Kind::kClipboardChunk || request.flags ||
        request.request_id != write_id_ || !write_id_ || request.value != pending_.size())
      return error("Clipboard write offset or request ID changed.");
    const size_t count = strlen(request.text);
    if (!count || count > expected_ - pending_.size())
      return error("Clipboard chunk exceeds the declared size.");
    pending_.append(request.text, count);
    reply.value = static_cast<uint32_t>(pending_.size());
    if (pending_.size() < expected_) { reply.flags = kClipboardReady; return reply; }
    if (!ClipboardUtf8(pending_)) return error("Clipboard text is not valid UTF-8.");
    write(pending_); Reset(); reply.flags = kClipboardComplete; return reply;
  }

 private:
  uint32_t read_id_ = 0, write_id_ = 0, expected_ = 0, read_offset_ = 0;
  std::string snapshot_, pending_;
};
}  // namespace aeraui::plugin_api
