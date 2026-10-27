/* SPDX-License-Identifier: Apache-2.0 */
// White-box lifetime test; the production scene exposes no test-only API.
#include "../scenes/generic_plugin_scene.cpp"
#include "plugin_activity_banner.hpp"
#include <cassert>
#include <chrono>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int stops = 0;
}
namespace aera::remote { std::string AccessCode() { return {}; } }
namespace aeraui::web {
void PreparePluginRuntime(Preparation &state, const char *, const char *, const char *, const char *) {
  state.verified = true;
  state.done.store(true, std::memory_order_release);
}
}
namespace aeraui::plugins {
bool HasPermission(const Plugin &plugin, const std::string &permission) {
  return std::find(plugin.permissions.begin(), plugin.permissions.end(), permission) !=
      plugin.permissions.end();
}
bool FindInstalled(const std::string &id, Plugin &plugin) {
  plugin.id = id; plugin.name = "OTA Explorer"; plugin.entry = "main";
  plugin.type = "ui-runtime"; return true;
}
}
namespace aeraui::plugin_api {
bool Process::Start(const std::string &, int &, std::string &) { assert(false); return false; }
bool Process::Running() { return true; }
void Process::Stop() { ++stops; }
bool OperationAllowed(const plugins::Plugin &, Operation) { return false; }
bool RunOperation(const plugins::Plugin &, Operation, std::string &) { return false; }
MirrorMode ActiveMirrorMode() { return MirrorMode::kOff; }
const char *OperationTitle(Operation) { return "Operation"; }
const char *OperationPrompt(Operation) { return "Confirm"; }
}
namespace aeraui {
void TestPluginRuntime(lv_obj_t *host_screen) {
  using namespace plugin_api;
  // Desktop support must be inert outside a desktop, and must release its
  // descriptors/UI cleanly without changing the normal plugin lifecycle.
  assert(!PluginDesktopHandlePointer(0, 10, 10, true));
  assert(!PluginDesktopHandleKey('a', 1));
  assert(!PluginDesktopHandleMouse(1, 10, 10, 1, true, 0, 0));
  int frame = -1, input = -1;
  assert(!OpenPluginDesktop(host_screen, frame, input, {}, 4096, 4096));
  assert(frame == -1 && input == -1);
  for (auto size : {std::pair<unsigned,unsigned>{640,360}, {960,540},
                    {1280,720}, {1920,1080}}) {
    assert(OpenPluginDesktop(host_screen, frame, input, {}, size.first, size.second));
    struct stat st{};
    assert(fstat(frame, &st) == 0);
    assert(st.st_size == static_cast<off_t>(size.first * size.second * 8));
    int duplicate_frame = -1, duplicate_input = -1;
    assert(!OpenPluginDesktop(host_screen, duplicate_frame, duplicate_input, {}));
    ClosePluginDesktop();
    close(frame); close(input);
    assert(!PluginDesktopHandleKey('a', 1));
  }
  auto send = [](int fd, Message message) {
    assert(::send(fd, &message, sizeof(message), MSG_NOSIGNAL) == sizeof(message));
  };
  auto receive = [](int fd) {
    Message message;
    assert(recv(fd, &message, sizeof(message), MSG_DONTWAIT) == sizeof(message));
    return message;
  };
  auto attach = [&](bool background, int &peer) {
    auto *page = lv_obj_create(nullptr);
    BuildGenericPluginScene(page, "test-ota", nullptr, nullptr);
    auto *scene = gPluginRuntimes.at("test-ota");
    for (int i = 0; i < 100 && !scene->preparation.done.load(std::memory_order_acquire); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(scene->preparation.done.load());
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) == 0);
    assert(scene->session.Adopt(sockets[0])); peer = sockets[1];
    scene->launched = true; scene->launched_ms = NowMs();
    Message hello; hello.kind = Kind::kHello; hello.value = hello.flags = 2;
    hello.request_id = background ? kFeatureRequestMagic | kOptionalFeatures : 0;
    send(peer, hello); scene->session.Poll();
    assert(receive(peer).flags == (background ? 3 | kOptionalFeatures : 3));
    assert(receive(peer).kind == Kind::kLifecycle);
    return page;
  };
  int peer = -1;
  auto *old = attach(false, peer);
  lv_screen_load(old);
  auto check_action_layout = [](GenericScene *scene, bool grid, bool readable) {
    scene->grid = grid;
    scene->readable = readable;
    scene->rows = {{RowKind::kMetric, 1, 0, "Storage", "Internal storage"}};
    scene->buttons.clear();
    for (uint32_t i = 0; i < 6; ++i) {
      scene->buttons.push_back({i + 1, 0, "Action " + std::to_string(i + 1),
          i % 2 ? "A longer description that wraps across multiple lines in the readable grid presentation" : ""});
    }
    RenderPage(scene);
    lv_obj_update_layout(scene->content);
    std::vector<lv_obj_t *> cards;
    for (uint32_t i = 0; i < lv_obj_get_child_count(scene->content); ++i) {
      auto *child = lv_obj_get_child(scene->content, i);
      if (lv_obj_check_type(child, &lv_button_class)) cards.push_back(child);
    }
    assert(cards.size() == scene->buttons.size());
    const bool two_columns = grid && lv_obj_get_width(scene->content) >= 900;
    for (size_t i = 0; i < cards.size(); ++i) {
      assert(lv_obj_get_height(cards[i]) >= 116);
      if (!grid && !readable)
        assert(lv_obj_get_height(cards[i]) == (i % 2 ? 148 : 116));
      if (two_columns && i % 2) {
        assert(lv_obj_get_y(cards[i]) == lv_obj_get_y(cards[i - 1]));
        assert(lv_obj_get_height(cards[i]) == lv_obj_get_height(cards[i - 1]));
        continue;
      }
      const size_t stride = two_columns ? 2 : 1;
      if (i >= stride)
        assert(lv_obj_get_y(cards[i]) >= lv_obj_get_y(cards[i - stride]) +
            lv_obj_get_height(cards[i - stride]) + 20);
    }
  };
  auto *legacy = gPluginRuntimes.at("test-ota");
  assert(!legacy->session.Supports(kFeatureRichPresentation));
  const std::string original_clipboard = text_edit::ClipboardText();
  text_edit::SetClipboardText("unchanged");
  Message clip; clip.kind = Kind::kGetClipboard; clip.request_id = 81;
  HandleMessage(legacy, clip);
  assert(receive(peer).flags == kClipboardError);
  assert(text_edit::ClipboardText() == "unchanged");
  check_action_layout(legacy, false, false);
  SetBackAction(legacy, 71);
  assert(lv_obj_get_user_data(legacy->card) == &widgets::kPageBackMarker);
  assert(lv_obj_get_user_data(legacy->card) != &widgets::kModalMarker);
  assert(lv_obj_get_user_data(legacy->card) != &widgets::kPersistentModalMarker);
  auto *modal = lv_obj_create(old);
  lv_obj_set_user_data(modal, &widgets::kPersistentModalMarker);
  bool modal_cancelled = false;
  lv_obj_add_event_cb(modal, [](lv_event_t *event) {
    *static_cast<bool *>(lv_event_get_user_data(event)) = true;
  }, LV_EVENT_CANCEL, &modal_cancelled);
  assert(widgets::DismissModal(old));
  assert(modal_cancelled && legacy->page_back_action == 71);
  lv_obj_delete(modal);
  assert(widgets::DismissModal(old));
  auto back = receive(peer);
  assert(back.kind == Kind::kAction && back.request_id == 71);
  assert(legacy->page_back_action == 0);
  lv_screen_load(host_screen);
  lv_obj_delete(old);
  assert(gPluginRuntimes.empty());
  close(peer);

  auto *page = attach(true, peer);
  lv_screen_load(page);
  auto *scene = gPluginRuntimes.at("test-ota");
  auto *editor = lv_textarea_create(page);
  text_edit::Attach(editor);
  lv_textarea_set_text(editor, "Native editor to plugin");
  text_edit::SelectAll(editor); assert(text_edit::Copy(editor));
  HandleMessage(scene, clip);
  auto copied = receive(peer);
  assert(copied.flags == kClipboardComplete && std::string(copied.text) == "Native editor to plugin");
  clip.kind = Kind::kSetClipboard; clip.value = 18;
  HandleMessage(scene, clip); assert(receive(peer).flags == kClipboardReady);
  clip.kind = Kind::kClipboardChunk; clip.value = 0;
  snprintf(clip.text, sizeof(clip.text), "Plugin into editor");
  HandleMessage(scene, clip); assert(receive(peer).flags == kClipboardComplete);
  lv_textarea_set_text(editor, ""); assert(text_edit::Paste(editor));
  assert(std::string(lv_textarea_get_text(editor)) == "Plugin into editor");
  lv_obj_delete(editor);
  check_action_layout(scene, false, true);
  check_action_layout(scene, true, false);
  check_action_layout(scene, true, true);
  Message enable; enable.kind = Kind::kSetBackground; enable.request_id = 1; enable.value = 1;
  HandleMessage(scene, enable); assert(receive(peer).value == 1);
  Message storage; storage.kind = Kind::kSetActivityStorage; storage.request_id = 9;
  snprintf(storage.text, sizeof(storage.text), "/data/media/0/Download");
  HandleMessage(scene, storage);
  Message activity; activity.kind = Kind::kSetActivity; activity.request_id = 9;
  activity.value = 2500; activity.flags = kActivityActive | kActivityCancellable;
  snprintf(activity.title, sizeof(activity.title), "Downloading OTA");
  snprintf(activity.text, sizeof(activity.text), "250 MB / 1000 MB");
  HandleMessage(scene, activity); assert(receive(peer).value == 1);
  Message begin; begin.kind = Kind::kBeginPage;
  snprintf(begin.title, sizeof(begin.title), "Download"); HandleMessage(scene, begin);
  Message layout; layout.kind = Kind::kSetPageLayout; layout.value = 4;
  HandleMessage(scene, layout);
  assert(scene->grid && scene->readable);
  Message progress; progress.kind = Kind::kAddProgress; progress.request_id = 7;
  progress.value = 2500; snprintf(progress.title, sizeof(progress.title), "Progress");
  HandleMessage(scene, progress);
  Message tile; tile.kind = Kind::kAddTile; tile.request_id = 11;
  tile.value = static_cast<unsigned>(Icon::kHome);
  snprintf(tile.title, sizeof(tile.title), "Home"); HandleMessage(scene, tile);
  Message commit; commit.kind = Kind::kCommitPage; HandleMessage(scene, commit);
  assert(scene->rows[0].progress_bar);
  assert(lv_obj_get_style_bg_opa(scene->rows[0].progress_bar, LV_PART_INDICATOR) == LV_OPA_COVER);
  lv_obj_update_layout(page);
  assert(lv_obj_get_width(scene->rows[0].progress_bar) > 0);
  const int stopped_before_detach = stops;
  lv_screen_load(host_screen); lv_obj_delete(page);
  assert(gPluginRuntimes.at("test-ota") == scene);
  assert(!scene->screen && !scene->rows[0].value_label && !scene->rows[0].progress_bar);
  assert(stops == stopped_before_detach);
  assert(receive(peer).value == static_cast<unsigned>(Lifecycle::kPause));
  clip.kind = Kind::kGetClipboard; clip.value = 0; clip.text[0] = 0;
  HandleMessage(scene, clip); assert(receive(peer).flags == kClipboardError);
  text_edit::SetClipboardText(original_clipboard);
  progress.kind = Kind::kUpdateProgress; progress.value = 7500;
  send(peer, progress);
  activity.value = 7500; send(peer, activity);
  PollGenericPluginRuntimes();
  assert(scene->rows[0].progress == 7500);
  assert(Activities().front().progress == 7500);

  auto *reattached = lv_obj_create(nullptr);
  BuildGenericPluginScene(reattached, "test-ota", nullptr, nullptr);
  assert(gPluginRuntimes.at("test-ota") == scene);
  assert(receive(peer).value == static_cast<unsigned>(Lifecycle::kResume));
  assert(lv_bar_get_value(scene->rows[0].progress_bar) == 7500);
  // Reattach before an animated old screen is deleted: its callback must not
  // detach the replacement view or delete its worker.
  auto *replacement = lv_obj_create(nullptr);
  BuildGenericPluginScene(replacement, "test-ota", nullptr, nullptr);
  assert(receive(peer).value == static_cast<unsigned>(Lifecycle::kResume));
  lv_obj_delete(reattached);
  assert(scene->screen == replacement);
  assert(CancelGenericPluginActivity("test-ota"));
  assert(receive(peer).kind == Kind::kActivityCancel);
  assert(Activities().front().cancelling);
  widgets::PluginActivityBanner banner;
  banner.Refresh(true, nullptr, nullptr);
  banner.Open(); assert(banner.Expanded());
  banner.Refresh(true, nullptr, nullptr);
  for (int frame = 0; frame < 160; ++frame) {
    activity.value = frame * 60;
    snprintf(activity.text, sizeof(activity.text), "%d / 10240 MB / 123.4 MB/s", frame * 60);
    HandleMessage(scene, activity);
    banner.Refresh(true, nullptr, nullptr);
    lv_tick_inc(20); lv_timer_handler();
  }
  banner.Close(); assert(!banner.Expanded());
  ShutdownGenericPluginRuntimes();
  assert(!PluginStorageBusy());
  lv_obj_delete(replacement);
  assert(gPluginRuntimes.empty()); close(peer);
  banner.Clear();
  Message complete = activity; complete.flags = kActivityComplete; complete.value = 10000;
  std::string error;
  assert(SetActivity("completed", "OTA Explorer", "", complete, error));
  // Leave the host-managed details open for the native screenshot fixture.
  static widgets::PluginActivityBanner preview;
  preview.Refresh(true, nullptr, nullptr); preview.Open();
  lv_obj_update_layout(lv_layer_top());
  auto find_label = [](auto &&self, lv_obj_t *root, const char *text) -> lv_obj_t * {
    if (lv_obj_check_type(root, &lv_label_class) &&
        !strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
      if (auto *found = self(self, lv_obj_get_child(root, i), text)) return found;
    return nullptr;
  };
  auto *title = find_label(find_label, lv_layer_top(), "OTA Explorer / Downloading OTA");
  assert(title);
  lv_area_t bounds; lv_obj_get_coords(title, &bounds);
  assert(bounds.x1 >= 0 && bounds.x2 < 1440 && bounds.y1 > StatusBarHeight());
  assert(lv_obj_get_width(lv_obj_get_parent(title)) > 1000);
}
}
