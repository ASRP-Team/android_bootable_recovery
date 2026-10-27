/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstdint>
#include <cstring>

namespace aeraui::plugin_api {

constexpr uint32_t kMagic = 0x41325049U;  // A2PI
constexpr uint32_t kProtocolVersion = 2;
constexpr uint32_t kMaxButtons = 12;
constexpr uint32_t kMaxMetrics = 64;
constexpr uint32_t kMaxSections = 16;

enum Features : uint32_t {
  kFeatureMetrics = 1U << 0,
  kFeatureBackNavigation = 1U << 1,
  kFeatureBackground = 1U << 2,
  kFeatureActivities = 1U << 3,
  kFeatureRichPresentation = 1U << 4,
  kFeaturePackageReview = 1U << 5,
  kFeatureReadablePresentation = 1U << 6,
  kFeatureDesktop = 1U << 7,
  kFeatureDesktopResolution = 1U << 8,
  kFeatureClipboard = 1U << 9,
};
constexpr uint32_t kOptionalFeatures = kFeatureBackground | kFeatureActivities |
    kFeatureRichPresentation | kFeaturePackageReview | kFeatureReadablePresentation |
    kFeatureDesktop | kFeatureDesktopResolution | kFeatureClipboard;
constexpr uint32_t kFeatureRequestMagic = 0xA2E20000U;

enum class Kind : uint32_t {
  kHello = 1,
  kBeginPage,
  kAddButton,
  kCommitPage,
  kSetStatus,
  kRequestOperation,
  kClose,
  kAddSection,
  kAddMetric,
  kUpdateMetric,
  kSetBackAction,
  kSetBackground,
  kSetActivity,
  kSetActivityStorage,
  kAddProgress,
  kUpdateProgress,
  kAddTile,
  kSetPageLayout,
  kSetActivityFile,
  kReviewPackage,
  kOpenDesktop = 21,
  kGetClipboard = 22,
  kSetClipboard = 23,
  kClipboardChunk = 24,
  kHelloAck = 64,
  kAction,
  kLifecycle,
  kOperationResult,
  kBackgroundResult,
  kActivityCancel,
  kActivityResult,
  kActivityFileResult,
  kDesktopReady = 72,
  kClipboardResult = 73,
};

constexpr uint32_t kMaxClipboardBytes = 1024U * 1024U;
constexpr uint32_t kClipboardContinue = 1U;
enum ClipboardResultFlags : uint32_t {
  kClipboardReady = 1U << 0,
  kClipboardMore = 1U << 1,
  kClipboardComplete = 1U << 2,
  kClipboardError = 1U << 3,
};

constexpr uint32_t kProgressUnknown = UINT32_MAX;
enum ActivityFlags : uint32_t {
  kActivityActive = 1U << 0,
  kActivityCancellable = 1U << 1,
  kActivityComplete = 1U << 2,
  kActivityFailed = 1U << 3,
};
enum class Icon : uint32_t {
  kDefault, kHome, kBack, kDownload, kFolder, kRefresh, kInfo,
  kNetwork, kStorage, kImage, kSettings, kStop,
};

enum class Lifecycle : uint32_t { kResume = 1, kPause, kStop };
// Operation numbers are part of the public Host API 2 wire contract. Never
// renumber an existing entry: independently released plugins send these raw
// values over the socket.
enum class Operation : uint32_t {
  kBackupSettings = 1,
  kRestoreSettings = 2,
  kBackupAndroidSettings = 3,
  kRestoreAndroidSettings = 4,
  kStartMirror = 5,
  kStopMirror = 6,
  kStartWifiMirror = 7,
};
enum Flags : uint32_t {
  kPrimary = 1U << 0,
  kDestructive = 1U << 1,
  kDisabled = 1U << 2,
  kMetricGood = 1U << 8,
  kMetricWarning = 1U << 9,
  kMetricCritical = 1U << 10,
  kMetricAccent = 1U << 11,
};

// One bounded packet is one complete message. Strings must be NUL-terminated;
// No pointers or variable-size allocations cross the channel. Optional storage
// paths use the bounded text field.
// The optional host-to-worker kDesktopReady message alone carries two
// SCM_RIGHTS descriptors: host-allocated, size-sealed shared pixels and input.
struct Message {
  uint32_t magic = kMagic;
  uint32_t version = kProtocolVersion;
  Kind kind = Kind::kSetStatus;
  uint32_t request_id = 0;
  uint32_t value = 0;
  uint32_t flags = 0;
  char title[96]{};
  char text[1024]{};
};

static_assert(sizeof(Message) == 1144);

inline bool StringsValid(const Message &message) {
  return memchr(message.title, '\0', sizeof(message.title)) != nullptr &&
         memchr(message.text, '\0', sizeof(message.text)) != nullptr;
}
inline bool WorkerKind(Kind kind) {
  return kind >= Kind::kHello && kind <= Kind::kClipboardChunk;
}
inline bool HostKind(Kind kind) {
  return kind >= Kind::kHelloAck && kind <= Kind::kClipboardResult;
}
inline bool Valid(const Message &message, bool from_worker) {
  return message.magic == kMagic && message.version == kProtocolVersion &&
         StringsValid(message) &&
         (from_worker ? WorkerKind(message.kind) : HostKind(message.kind));
}

}  // namespace aeraui::plugin_api
