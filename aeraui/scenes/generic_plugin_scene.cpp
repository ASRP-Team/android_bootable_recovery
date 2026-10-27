/* SPDX-License-Identifier: Apache-2.0 */
#include "scene.hpp"
#include "package_review.hpp"
#include "../../aera_remote/aera_remote.hpp"

#include "browser/runtime.hpp"
#include "plugin_api/launcher.hpp"
#include "plugin_api/operations.hpp"
#include "plugin_api/protocol.hpp"
#include "plugin_api/session.hpp"
#include "plugin_api/clipboard.hpp"
#include "text_edit.hpp"
#include "plugin_api/activity.hpp"
#include "plugin_api/runtime.hpp"
#include "plugins/plugin_manager.hpp"
#include "ui_components.hpp"
#include "plugin_desktop.hpp"
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

namespace aeraui {
namespace {
using namespace design;
using namespace widgets;

std::mutex gSelectionMutex;
std::string gSelectedPlugin;
struct GenericScene;
struct ViewBinding { GenericScene *scene = nullptr; };
std::map<std::string, GenericScene *> gPluginRuntimes;

uint64_t NowMs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

struct ButtonModel {
  uint32_t id = 0;
  uint32_t flags = 0;
  std::string title;
  std::string detail;
  plugin_api::Icon icon = plugin_api::Icon::kDefault;
  bool tile = false;
};

enum class RowKind { kSection, kMetric, kProgress };

struct RowModel {
  RowKind kind = RowKind::kMetric;
  uint32_t id = 0;
  uint32_t flags = 0;
  std::string title;
  std::string value;
  lv_obj_t *value_label = nullptr;
  lv_obj_t *progress_bar = nullptr;
  uint32_t progress = 0;
};

struct GenericScene {
  bool desktop_open = false;
  lv_obj_t *screen = nullptr;
  lv_obj_t *card = nullptr;
  lv_obj_t *content = nullptr;
  lv_obj_t *status = nullptr;
  lv_obj_t *progress = nullptr;
  lv_obj_t *mirror_tabs[2] = {nullptr, nullptr};
  uint32_t mirror_selection = 0;
  std::string mirror_access_code;
  uint64_t mirror_code_checked_ms = 0;
  plugins::Plugin plugin;
  web::Preparation preparation;
  plugin_api::Process process;
  plugin_api::Session session;
  plugin_api::ClipboardTransfer clipboard;
  std::thread worker;
  lv_timer_t *timer = nullptr;
  std::string page_title;
  std::string page_body;
  std::vector<ButtonModel> buttons;
  std::vector<RowModel> rows;
  uint32_t page_back_action = 0;
  uint32_t pending_back_action = 0;
  uint64_t launched_ms = 0;
  uint64_t operation_started_ms = 0;
  uint32_t operation_request = 0;
  bool launched = false;
  bool page_pending = false;
  bool operation_pending = false;
  bool stopped = false;
  bool background = false, grid = false, readable = false;
  ViewBinding *binding = nullptr;
  std::string last_status, activity_storage;
  uint32_t activity_id = 0;
  uint64_t cancel_started_ms = 0;
  ActionCallback callback = nullptr;
  void *context = nullptr;

  ~GenericScene() {
    if (binding) binding->scene = nullptr;
    auto found = gPluginRuntimes.find(plugin.id);
    if (found != gPluginRuntimes.end() && found->second == this)
      gPluginRuntimes.erase(found);
    if (desktop_open) ClosePluginDesktop();
    if (timer) lv_timer_delete(timer);
    preparation.cancel.store(true);
    if (worker.joinable()) worker.join();
    if (session.Connected()) {
      session.Send(plugin_api::Kind::kLifecycle, 0,
                   static_cast<uint32_t>(plugin_api::Lifecycle::kStop));
      session.Send(plugin_api::Kind::kClose);
    }
    session.Close();
    process.Stop();
    plugin_api::RemoveActivity(plugin.id);
    web::RemoveRuntime(preparation.directory);
  }
};

void SetStatus(GenericScene *scene, const std::string &status) {
  scene->last_status = status;
  if (scene->status) i18n::BindLabel(scene->status, status.c_str());
}

void SetBackAction(GenericScene *scene, uint32_t action) {
  scene->page_back_action = action;
  if (scene->card)
    lv_obj_set_user_data(scene->card,
                         action ? &kPageBackMarker : nullptr);
}

void RequestPage(GenericScene *scene, uint32_t action) {
  if (!action || !scene->session.Connected()) return;
  // Mirror Host API 2 actions 1/2 select the Wi-Fi/USB guide; they do not
  // start the server. Start actions still use the host approval sheet.
  if (scene->plugin.id == "mirror" && (action == 1 || action == 2))
    scene->mirror_selection = action;
  SetBackAction(scene, 0);
  SetStatus(scene, "Working...");
  if (scene->progress) {
    lv_bar_set_value(scene->progress, 24, LV_ANIM_OFF);
    lv_bar_set_value(scene->progress, 72, LV_ANIM_ON);
  }
  scene->session.Send(plugin_api::Kind::kAction, action);
}

void MirrorModeCard(GenericScene *scene, int index, int x, int y, int width) {
  auto *card = lv_button_create(scene->screen);
  scene->mirror_tabs[index] = card;
  Panel(card, 34, kMainPanel);
  Interactive(card, kMainSelected);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, 220);
  auto *symbol = Label(card, index == 0 ? LV_SYMBOL_WIFI : LV_SYMBOL_USB,
                        &lv_font_montserrat_48, kAccent);
  lv_obj_set_pos(symbol, 34, 30);
  auto *heading = Label(card, index == 0 ? "Wi-Fi Mirror" : "USB Mirror",
                         &lv_font_montserrat_32, kText);
  lv_obj_set_pos(heading, 112, 28);
  SingleLineLabel(heading, width - 176, &lv_font_montserrat_32);
  auto *copy = Label(card, index == 0
      ? "Connect from a browser on your Wi-Fi network."
      : "Connect through ADB and the desktop launcher.",
      &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(copy, 34, 112);
  FitLabelToLines(copy, width - 68, 2,
      {&lv_font_montserrat_24, &lv_font_montserrat_20, &lv_font_montserrat_18});
  OnClick(card, [scene, index] { RequestPage(scene, index + 1); });
}

bool ModalVisible(lv_obj_t *screen) {
  for (uint32_t index = 0; index < lv_obj_get_child_count(screen); ++index) {
    auto *child = lv_obj_get_child(screen, static_cast<int32_t>(index));
    if (lv_obj_get_user_data(child) == &kModalMarker &&
        !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) return true;
  }
  return false;
}

const char *ActionIcon(const ButtonModel &model) {
  if (model.tile) switch (model.icon) {
    case plugin_api::Icon::kHome: return LV_SYMBOL_HOME;
    case plugin_api::Icon::kBack: return LV_SYMBOL_LEFT;
    case plugin_api::Icon::kDownload: return LV_SYMBOL_DOWNLOAD;
    case plugin_api::Icon::kFolder: return LV_SYMBOL_DIRECTORY;
    case plugin_api::Icon::kRefresh: return LV_SYMBOL_REFRESH;
    case plugin_api::Icon::kInfo: return LV_SYMBOL_LIST;
    case plugin_api::Icon::kNetwork: return LV_SYMBOL_WIFI;
    case plugin_api::Icon::kStorage: return LV_SYMBOL_DRIVE;
    case plugin_api::Icon::kImage: return LV_SYMBOL_IMAGE;
    case plugin_api::Icon::kSettings: return LV_SYMBOL_SETTINGS;
    case plugin_api::Icon::kStop: return LV_SYMBOL_CLOSE;
    default: break;
  }
  if (model.title.find("Wi-Fi") != std::string::npos)
    return LV_SYMBOL_WIFI;
  if (model.title.find("USB") != std::string::npos)
    return LV_SYMBOL_USB;
  if (model.title.find("Stop") != std::string::npos)
    return LV_SYMBOL_CLOSE;
  if (model.title.find("another") != std::string::npos ||
      model.title.find("Return") != std::string::npos)
    return LV_SYMBOL_LEFT;
  if (model.title.find("Start") != std::string::npos ||
      model.title.find("Restart") != std::string::npos)
    return LV_SYMBOL_PLAY;
  return LV_SYMBOL_RIGHT;
}

lv_obj_t *ActionCard(GenericScene *scene, const ButtonModel &model, int width,
                     int y, size_t index) {
  const bool primary = (model.flags & plugin_api::kPrimary) != 0;
  const bool destructive = (model.flags & plugin_api::kDestructive) != 0;
  const bool disabled = (model.flags & plugin_api::kDisabled) != 0;
  auto *card = lv_button_create(scene->content);
  Panel(card, 34, kMainSheet);
  Interactive(card, primary ? kMainSelected : kMainPanel);
  lv_obj_set_pos(card, 36, y);
  lv_obj_set_size(card, width, model.detail.empty() ? 116 : 148);
  lv_obj_set_style_border_width(card, primary ? 2 : 1, 0);
  lv_obj_set_style_border_color(card,
      destructive ? kRed : primary ? kAccent : kMainLine, 0);
  lv_obj_set_style_border_opa(card,
      primary ? LV_OPA_60 : destructive ? LV_OPA_40 : LV_OPA_30, 0);
  lv_obj_set_style_transform_scale(card, 254, LV_STATE_PRESSED);
  auto *plate = IconPlate(card, ActionIcon(model),
                          destructive ? kRed : primary ? kAccent : kText,
                          primary ? kMainPanel : kMainCanvas, 76);
  lv_obj_align(plate, LV_ALIGN_LEFT_MID, 24, 0);
  auto *title = Label(card, model.title.c_str(), scene->readable ? &lv_font_montserrat_32 : model.tile ? &lv_font_montserrat_24 : &lv_font_montserrat_32,
                      destructive ? kRed : kText);
  lv_obj_set_pos(title, 124, model.detail.empty() ? 38 : 28);
  lv_obj_set_width(title, width - 210);
  if (model.tile && !scene->readable) SingleLineLabel(title, width - 210, &lv_font_montserrat_24);
  lv_obj_t *detail = nullptr;
  if (!model.detail.empty()) {
    detail = Label(card, model.detail.c_str(),
                         scene->readable ? &lv_font_montserrat_28 : model.tile ? &lv_font_montserrat_20 : &lv_font_montserrat_24, kMuted);
    lv_obj_set_pos(detail, 124, 78);
    lv_obj_set_width(detail, width - 210);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
  }
  if (scene->readable) {
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_update_layout(title);
    int bottom = lv_obj_get_y(title) + lv_obj_get_height(title);
    if (detail) {
      lv_obj_set_y(detail, bottom + 12);
      lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
      lv_obj_update_layout(detail);
      bottom = lv_obj_get_y(detail) + lv_obj_get_height(detail);
    }
    lv_obj_set_height(card, std::max(148, bottom + 28));
  }
  auto *arrow = Label(card, LV_SYMBOL_RIGHT, &lv_font_montserrat_24,
                      primary ? kAccent : kDim);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -28, 0);
  OnClick(card, [scene, id = model.id, disabled] {
    if (!disabled) RequestPage(scene, id);
  });
  if (disabled) lv_obj_add_state(card, LV_STATE_DISABLED);
  AnimateEnter(card, 25 + static_cast<uint32_t>(index) * 26, 10);
  return card;
}

lv_color_t MetricColor(uint32_t flags) {
  if (flags & plugin_api::kMetricCritical) return kRed;
  if (flags & plugin_api::kMetricWarning) return kAmber;
  if (flags & plugin_api::kMetricGood) return kGreen;
  if (flags & plugin_api::kMetricAccent) return kAccent;
  return kText;
}

bool MetricFlagsValid(uint32_t flags) {
  constexpr uint32_t mask = plugin_api::kMetricGood |
      plugin_api::kMetricWarning | plugin_api::kMetricCritical |
      plugin_api::kMetricAccent;
  const uint32_t tone = flags & mask;
  return (flags & ~mask) == 0 && (tone == 0 || (tone & (tone - 1)) == 0);
}

void RenderMetricPage(GenericScene *scene) {
  lv_obj_clean(scene->content);
  lv_obj_update_layout(scene->content);
  const int content_width = lv_obj_get_width(scene->content);
  const size_t metric_count = std::count_if(
      scene->rows.begin(), scene->rows.end(), [](const RowModel &row) {
        return row.kind == RowKind::kMetric;
      });
  const bool animate_rows = metric_count <= 20;
  auto *title = Label(scene->content,
                      scene->page_title.empty() ? scene->plugin.name.c_str()
                                                : scene->page_title.c_str(),
                      scene->readable ? &lv_font_montserrat_48 : &lv_font_montserrat_40, kText);
  lv_obj_set_pos(title, 36, 22);
  lv_obj_set_width(title, std::max(400, content_width - 72));
  int y = 92;
  if (!scene->page_body.empty()) {
    auto *body = Label(scene->content, scene->page_body.c_str(),
                       scene->readable ? &lv_font_montserrat_32 : &lv_font_montserrat_28, kMutedStrong);
    lv_obj_set_pos(body, 36, y);
    lv_obj_set_width(body, std::max(400, content_width - 72));
    lv_obj_set_style_text_line_space(body, 8, 0);
    lv_obj_update_layout(body);
    y += static_cast<int>(lv_obj_get_height(body)) + 30;
  }
  size_t visible_index = 0;
  for (auto &model : scene->rows) {
    model.value_label = nullptr;
    model.progress_bar = nullptr;
    if (model.kind == RowKind::kSection) {
      if (visible_index != 0) y += 18;
      auto *section = Label(scene->content, model.title.c_str(),
                            scene->readable ? &lv_font_montserrat_32 : &lv_font_montserrat_24, kAccent);
      lv_obj_set_pos(section, 36, y + 12);
      lv_obj_set_width(section, std::max(400, content_width - 72));
      lv_obj_set_style_text_letter_space(section, 2, 0);
      y += scene->readable ? 68 : 56;
      continue;
    }
    if (model.kind == RowKind::kProgress) {
      const int row_width = std::max(400, content_width - 72);
      const auto *font = scene->readable ? &lv_font_montserrat_32 : &lv_font_montserrat_28;
      auto *name = Label(scene->content, model.title.c_str(), font, kText);
      lv_obj_set_pos(name, 36, y + 12); SingleLineLabel(name, row_width, font);
      model.progress_bar = lv_bar_create(scene->content);
      lv_obj_set_pos(model.progress_bar, 36, y + 66);
      lv_obj_set_size(model.progress_bar, row_width, 10);
      lv_bar_set_range(model.progress_bar, 0, 10000);
      lv_obj_set_style_bg_color(model.progress_bar, kMainLine, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(model.progress_bar, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(model.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(model.progress_bar, MetricColor(model.flags), LV_PART_INDICATOR);
      lv_bar_set_value(model.progress_bar, model.progress, LV_ANIM_OFF);
      const auto *detail_font = scene->readable ? &lv_font_montserrat_32 : &lv_font_montserrat_24;
      model.value_label = Label(scene->content, model.value.c_str(), detail_font, kMutedStrong);
      lv_obj_set_pos(model.value_label, 36, y + 96);
      SingleLineLabel(model.value_label, row_width, detail_font);
      y += scene->readable ? 168 : 152; ++visible_index; continue;
    }
    const int row_width = std::max(400, content_width - 72);
    const int column_gap = 36;
    const int name_width = std::max(180, (row_width - column_gap) * 2 / 5);
    const int value_width = std::max(180, row_width - name_width - column_gap);
    auto *row = lv_obj_create(scene->content);
    Clear(row);
    lv_obj_set_pos(row, 36, y);
    lv_obj_set_width(row, row_width);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, kMainLine, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_20, 0);
    const auto *font = scene->readable ? &lv_font_montserrat_40 : &lv_font_montserrat_32;
    auto *name = Label(row, model.title.c_str(), font,
                       kMutedStrong);
    lv_obj_set_pos(name, 0, 20);
    lv_obj_set_width(name, name_width);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_WRAP);
    model.value_label = Label(row, model.value.c_str(),
                              font,
                              MetricColor(model.flags));
    lv_obj_set_pos(model.value_label, name_width + column_gap, 20);
    lv_obj_set_width(model.value_label, value_width);
    lv_obj_set_style_text_align(model.value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(model.value_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_update_layout(row);
    const int row_height = std::max(
        82, std::max(static_cast<int>(lv_obj_get_height(name)),
                     static_cast<int>(lv_obj_get_height(model.value_label))) + 40);
    lv_obj_set_height(row, row_height);
    if (animate_rows)
      AnimateEnter(row, 12 + static_cast<uint32_t>(visible_index) * 10, 6);
    ++visible_index;
    y += row_height;
  }
  if (!scene->buttons.empty()) y += 26;
  lv_obj_t *previous_card = nullptr;
  for (size_t index = 0; index < scene->buttons.size(); ++index) {
    const auto &model = scene->buttons[index];
    const bool grid = scene->grid && content_width >= 900;
    const int width = grid ? (content_width - 96) / 2 : std::max(400, content_width - 72);
    auto *card = ActionCard(scene, model, width, y, visible_index + index);
    if (grid) {
      lv_obj_set_x(card, 36 + static_cast<int>(index % 2) * (width + 24));
      if (!scene->readable) lv_obj_set_height(card, 148);
    }
    // Resolve deferred sizes before placing the next action or grid row.
    lv_obj_update_layout(card);
    if (grid) {
      if (index % 2) {
        const int height = std::max(lv_obj_get_height(card), lv_obj_get_height(previous_card));
        lv_obj_set_height(previous_card, height); lv_obj_set_height(card, height);
        y += height + 20;
      } else if (index + 1 == scene->buttons.size()) y += lv_obj_get_height(card) + 20;
      previous_card = card;
    } else y += lv_obj_get_height(card) + 20;
  }
}

void RenderPage(GenericScene *scene) {
  if (!scene->content) return;
  if (!scene->rows.empty() || scene->grid) {
    RenderMetricPage(scene);
    return;
  }
  lv_obj_clean(scene->content);
  // LVGL defers coordinate resolution until a layout pass. Generic plugin
  // pages can commit during the very first timer tick, so resolve the explicit
  // content size before using it for child widths.
  lv_obj_update_layout(scene->content);
  const int content_width = lv_obj_get_width(scene->content);
  const bool mirror = scene->plugin.id == "mirror";
  if (mirror) {
    scene->mirror_access_code = plugin_api::ActiveMirrorMode() ==
        plugin_api::MirrorMode::kOff ? "" : aera::remote::AccessCode();
  }
  if (mirror) {
    for (int index = 0; index < 2; ++index) {
      auto *tab = scene->mirror_tabs[index];
      if (!tab) continue;
      const bool selected = scene->mirror_selection == static_cast<uint32_t>(index + 1);
      lv_obj_set_style_border_width(tab, selected ? 3 :
          RecoverySurfaceStyle() == SurfaceStyle::kOutline ? 2 : 1, 0);
      lv_obj_set_style_border_color(tab, selected ? kAccent : kMainLine, 0);
      lv_obj_set_style_border_opa(tab, selected ? LV_OPA_COVER : LV_OPA_50, 0);
    }
  }
  auto *title = Label(scene->content,
                      scene->page_title.empty() ? scene->plugin.name.c_str()
                                                : scene->page_title.c_str(),
                      &lv_font_montserrat_48, kText);
  lv_obj_set_pos(title, 36, 28);
  lv_obj_set_width(title, std::max(400, content_width - 72));
  auto *body_panel = lv_obj_create(scene->content);
  Panel(body_panel, 36, kMainSheet);
  lv_obj_set_pos(body_panel, 36, 108);
  lv_obj_set_width(body_panel, std::max(400, content_width - 72));
  lv_obj_set_style_border_width(body_panel, 1, 0);
  lv_obj_set_style_border_color(body_panel, kMainLine, 0);
  lv_obj_set_style_border_opa(body_panel, LV_OPA_30, 0);
  auto *body_icon = IconPlate(body_panel,
      mirror ? LV_SYMBOL_VIDEO : LV_SYMBOL_SETTINGS,
      mirror ? kAccent : kText, kMainPanel, 88);
  lv_obj_set_pos(body_icon, 30, 32);
  auto *eyebrow = Label(body_panel,
      mirror ? "PRIVATE RECOVERY MIRROR" : "PLUGIN SESSION",
      &lv_font_montserrat_18, mirror ? kAccent : kMuted);
  lv_obj_set_pos(eyebrow, 146, 30);
  lv_obj_set_style_text_letter_space(eyebrow, 3, 0);
  if (mirror) {
    const bool live = plugin_api::ActiveMirrorMode() !=
                      plugin_api::MirrorMode::kOff;
    auto *state = Label(body_panel, live ? "●  LIVE" : "READY",
                        &lv_font_montserrat_18, live ? kGreen : kAccent);
    lv_obj_align(state, LV_ALIGN_TOP_RIGHT, -30, 30);
    lv_obj_set_style_text_letter_space(state, 2, 0);
  }
  std::string body_copy = scene->page_body;
  std::string address;
  if (mirror) {
    const size_t url = body_copy.find("http://");
    if (url != std::string::npos) {
      const size_t end = body_copy.find('\n', url);
      address = body_copy.substr(url, end == std::string::npos
                                         ? std::string::npos : end - url);
      const size_t line_start = body_copy.rfind('\n', url);
      body_copy.erase(line_start == std::string::npos ? 0 : line_start + 1,
                      end == std::string::npos
                          ? std::string::npos
                          : end - (line_start == std::string::npos
                                       ? 0 : line_start + 1) + 1);
    }
  }
  const int body_y = address.empty() ? 70 : 154;
  if (!address.empty()) {
    auto *address_plate = lv_obj_create(body_panel);
    Panel(address_plate, 22, kMainPanel);
    lv_obj_set_pos(address_plate, 146, 70);
    lv_obj_set_size(address_plate, std::max(250, content_width - 316), 64);
    auto *address_label = Label(address_plate, address.c_str(),
                                &lv_font_montserrat_32, kAccent);
    lv_obj_align(address_label, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_width(address_label, std::max(200, content_width - 356));
    lv_label_set_long_mode(address_label, LV_LABEL_LONG_DOT);
  }
  auto *body = Label(body_panel, body_copy.c_str(),
                     &lv_font_montserrat_32, kMutedStrong);
  lv_obj_set_pos(body, 146, body_y);
  lv_obj_set_width(body, std::max(280, content_width - 270));
  lv_obj_set_style_text_line_space(body, 14, 0);
  lv_obj_update_layout(body);
  const int panel_height = std::max(170,
      body_y + 48 + static_cast<int>(lv_obj_get_height(body)));
  lv_obj_set_height(body_panel, panel_height);
  AnimateEnter(body_panel, 10, 10);
  int y = 134 + panel_height;
  if (mirror && !scene->mirror_access_code.empty()) {
    // Read the actual running session's code for both transports, rather
    // than relying on the plugin's Wi-Fi-only result URL.
    auto *caption = Label(scene->content, "Access code",
                           &lv_font_montserrat_24, kMutedStrong);
    lv_obj_set_pos(caption, 36, y + 8);
    lv_obj_set_width(caption, std::max(400, content_width - 72));
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    auto *code = Label(scene->content, scene->mirror_access_code.c_str(),
                        &lv_font_montserrat_48, kText);
    lv_obj_set_pos(code, 36, y + 48);
    lv_obj_set_width(code, std::max(400, content_width - 72));
    lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(code, 8, 0);
    y += 138;
  }
  const ButtonModel *stop_button = nullptr;
  size_t visible_index = 0;
  for (size_t index = 0; index < scene->buttons.size(); ++index) {
    const auto &model = scene->buttons[index];
    // The persistent mode cards replace Mirror's guide/home navigation.
    if (mirror && (model.id == 1 || model.id == 2 || model.id == 6))
      continue;
    if (mirror && model.id == 3) {
      if (plugin_api::ActiveMirrorMode() != plugin_api::MirrorMode::kOff)
        stop_button = &model;
      continue;
    }
    ActionCard(scene, model, std::max(400, content_width - 72), y,
               visible_index++);
    y += model.detail.empty() ? 136 : 168;
  }
  if (stop_button != nullptr) {
    const int stop_y = std::max(y + 24,
        static_cast<int>(lv_obj_get_height(scene->content)) - 184);
    ActionCard(scene, *stop_button, std::max(400, content_width - 72),
               stop_y, visible_index);
  }
}

void ReplyOperation(GenericScene *scene, plugin_api::Operation operation,
                    uint32_t request) {
  std::string result;
  const bool success = plugin_api::RunOperation(scene->plugin, operation, result);
  scene->session.Send(plugin_api::Kind::kOperationResult, request,
                      success ? 1U : 0U, 0, nullptr, result.c_str());
  SetStatus(scene, result);
  scene->operation_pending = false;
  scene->operation_request = 0;
}

void RequestOperation(GenericScene *scene,
                      const plugin_api::Message &message) {
  if (message.request_id == 0 || message.value <
          static_cast<uint32_t>(plugin_api::Operation::kBackupSettings) ||
      message.value >
          static_cast<uint32_t>(
              plugin_api::Operation::kStartWifiMirror)) {
    scene->session.Send(plugin_api::Kind::kOperationResult,
                        message.request_id, 0, 0, nullptr,
                        "Unsupported host operation.");
    return;
  }
  const auto operation = static_cast<plugin_api::Operation>(message.value);
  if (!plugin_api::OperationAllowed(scene->plugin, operation)) {
    scene->session.Send(plugin_api::Kind::kOperationResult,
                        message.request_id, 0, 0, nullptr,
                        "Permission denied by the AERA host.");
    return;
  }
  if (scene->operation_pending) {
    scene->session.Send(plugin_api::Kind::kOperationResult,
                        message.request_id, 0, 0, nullptr,
                        "Another host operation is waiting for approval.");
    return;
  }
  scene->operation_pending = true;
  scene->operation_request = message.request_id;
  scene->operation_started_ms = NowMs();
  SetStatus(scene, "Waiting for your approval");
  Sheet(scene->screen, plugin_api::OperationTitle(operation),
        plugin_api::OperationPrompt(operation),
        [scene, operation, request = message.request_id] {
          ReplyOperation(scene, operation, request);
        });
}

void HandleMessage(GenericScene *scene, const plugin_api::Message &message) {
  switch (message.kind) {
    case plugin_api::Kind::kGetClipboard:
    case plugin_api::Kind::kSetClipboard:
    case plugin_api::Kind::kClipboardChunk: {
      const bool allowed = scene->session.Supports(plugin_api::kFeatureClipboard) &&
          scene->screen && scene->screen == lv_screen_active() &&
          !scene->operation_pending && !ModalVisible(scene->screen);
      const auto reply = scene->clipboard.Handle(message, allowed,
          []() -> const std::string & { return text_edit::ClipboardText(); },
          [](const std::string &text) { text_edit::SetClipboardText(text); });
      scene->session.Send(reply.kind, reply.request_id, reply.value, reply.flags,
                          reply.title, reply.text);
      break;
    }
    case plugin_api::Kind::kOpenDesktop: {
      unsigned width=message.value, height=message.flags;
      if(!width&&!height){width=960;height=540;}
      if (!scene->screen || !scene->callback || scene->background ||
          !scene->session.Supports(plugin_api::kFeatureDesktop) ||
          !message.request_id || scene->desktop_open || scene->page_pending ||
          scene->operation_pending || !ValidDesktopResolution(width,height) ||
          !plugins::HasPermission(scene->plugin, "desktop-surface")) {
        scene->session.Send(plugin_api::Kind::kDesktopReady, message.request_id,
                            0, 0, nullptr, "Desktop request unavailable or not permitted.");
        break;
      }
      scene->callback(Action::kPluginDesktopOpen, scene->context);
      int frame=-1, input=-1;
      scene->desktop_open = OpenPluginDesktop(scene->screen, frame, input, [scene] {
        scene->desktop_open = false;
        scene->callback(Action::kPluginDesktopClose, scene->context);
      }, width, height);
      const bool sent = scene->desktop_open &&
          scene->session.SendDesktop(message.request_id, frame, input, width, height);
      if (frame >= 0) close(frame);
      if (input >= 0) close(input);
      if (!sent) {
        ClosePluginDesktop();
        scene->desktop_open = false;
        scene->callback(Action::kPluginDesktopClose, scene->context);
        scene->session.Send(plugin_api::Kind::kDesktopReady, message.request_id,
                            0, 0, nullptr, "Could not allocate desktop surface.");
      }
      break;
    }
    case plugin_api::Kind::kBeginPage:
      scene->page_title = message.title;
      scene->page_body = message.text;
      scene->buttons.clear();
      scene->rows.clear();
      scene->pending_back_action = 0;
      scene->page_pending = true;
      scene->grid = scene->readable = false;
      break;
    case plugin_api::Kind::kAddTile:
      if (!scene->session.Supports(plugin_api::kFeatureRichPresentation) ||
          message.value > static_cast<uint32_t>(plugin_api::Icon::kStop)) {
        scene->session.Close(); break;
      }
      [[fallthrough]];
    case plugin_api::Kind::kAddButton:
      if (!scene->page_pending || message.request_id == 0 ||
          message.title[0] == '\0' ||
          (message.flags & ~(plugin_api::kPrimary |
                             plugin_api::kDestructive |
                             plugin_api::kDisabled)) != 0 ||
          scene->buttons.size() >= plugin_api::kMaxButtons ||
          std::any_of(scene->buttons.begin(), scene->buttons.end(),
                      [&](const ButtonModel &button) {
                        return button.id == message.request_id;
                      })) {
        SetStatus(scene, "Plugin page exceeded the Host API limit");
        scene->session.Close();
        break;
      }
      scene->buttons.push_back({message.request_id, message.flags,
                                message.title, message.text,
                                static_cast<plugin_api::Icon>(message.value),
                                message.kind == plugin_api::Kind::kAddTile});
      break;
    case plugin_api::Kind::kAddSection:
      if (!scene->page_pending || message.title[0] == '\0' ||
          message.flags != 0 ||
          std::count_if(scene->rows.begin(), scene->rows.end(),
                        [](const RowModel &row) {
                          return row.kind == RowKind::kSection;
                        }) >= plugin_api::kMaxSections) {
        SetStatus(scene, "Plugin page exceeded the Host API limit");
        scene->session.Close();
        break;
      }
      scene->rows.push_back({RowKind::kSection, 0, 0, message.title,
                             message.text, nullptr});
      break;
    case plugin_api::Kind::kAddProgress:
      if (!scene->session.Supports(plugin_api::kFeatureRichPresentation) || message.value > 10000) {
        scene->session.Close(); break;
      }
      [[fallthrough]];
    case plugin_api::Kind::kAddMetric:
      if (!scene->page_pending || message.request_id == 0 ||
          message.title[0] == '\0' || !MetricFlagsValid(message.flags) ||
          std::count_if(scene->rows.begin(), scene->rows.end(),
                        [](const RowModel &row) {
                          return row.kind != RowKind::kSection;
                        }) >= plugin_api::kMaxMetrics ||
          std::any_of(scene->rows.begin(), scene->rows.end(),
                      [&](const RowModel &row) {
                        return row.kind != RowKind::kSection &&
                               row.id == message.request_id;
                      })) {
        SetStatus(scene, "Plugin page exceeded the Host API limit");
        scene->session.Close();
        break;
      }
      scene->rows.push_back({message.kind == plugin_api::Kind::kAddProgress ? RowKind::kProgress : RowKind::kMetric, message.request_id,
                             message.flags, message.title, message.text,
                             nullptr, nullptr, message.value});
      break;
    case plugin_api::Kind::kUpdateProgress:
      if (!scene->session.Supports(plugin_api::kFeatureRichPresentation) || message.value > 10000) {
        scene->session.Close(); break;
      }
      [[fallthrough]];
    case plugin_api::Kind::kUpdateMetric: {
      auto row = std::find_if(scene->rows.begin(), scene->rows.end(),
          [&](const RowModel &candidate) {
            return candidate.kind == (message.kind == plugin_api::Kind::kUpdateProgress ? RowKind::kProgress : RowKind::kMetric) &&
                   candidate.id == message.request_id;
          });
      if (scene->page_pending || row == scene->rows.end() ||
          !MetricFlagsValid(message.flags)) {
        SetStatus(scene, "Plugin sent an invalid metric update");
        scene->session.Close();
        break;
      }
      row->value = message.text;
      row->flags = message.flags;
      if (row->kind == RowKind::kProgress) {
        row->progress = message.value;
        if (row->progress_bar) {
          lv_bar_set_value(row->progress_bar, message.value, LV_ANIM_OFF);
          lv_obj_set_style_bg_color(row->progress_bar, MetricColor(row->flags), LV_PART_INDICATOR);
        }
      }
      if (row->value_label) {
        lv_label_set_text(row->value_label, row->value.c_str());
        lv_obj_set_style_text_color(row->value_label,
                                    MetricColor(row->flags), 0);
      }
      break;
    }
    case plugin_api::Kind::kSetBackAction:
      if (!scene->page_pending || message.request_id == 0 ||
          message.value != 0 || message.flags != 0 ||
          message.title[0] != '\0' || message.text[0] != '\0') {
        SetStatus(scene, "Plugin sent an invalid back action");
        scene->session.Close();
        break;
      }
      scene->pending_back_action = message.request_id;
      break;
    case plugin_api::Kind::kCommitPage:
      if (!scene->page_pending ||
          (scene->pending_back_action != 0 &&
           std::none_of(scene->buttons.begin(), scene->buttons.end(),
                        [scene](const ButtonModel &button) {
                          return button.id == scene->pending_back_action &&
                              !(button.flags & plugin_api::kDisabled);
                        }))) {
        SetStatus(scene, "Plugin sent an invalid Host API sequence");
        scene->session.Close();
        break;
      }
      scene->page_pending = false;
      if (scene->plugin.id == "mirror" &&
          std::any_of(scene->buttons.begin(), scene->buttons.end(),
                      [](const ButtonModel &button) { return button.id == 1; }) &&
          std::any_of(scene->buttons.begin(), scene->buttons.end(),
                      [](const ButtonModel &button) { return button.id == 2; })) {
        // Open the selected guide directly, including after Stop. Selecting
        // a mode never starts sharing; the guide retains its Start action.
        RequestPage(scene, scene->mirror_selection ? scene->mirror_selection :
            plugin_api::ActiveMirrorMode() == plugin_api::MirrorMode::kUsb ? 2 : 1);
        break;
      }
      SetBackAction(scene, scene->pending_back_action);
      RenderPage(scene);
      if (scene->progress)
        lv_bar_set_value(scene->progress, 100, LV_ANIM_ON);
      SetStatus(scene, "Connected through AERA Host API 2");
      break;
    case plugin_api::Kind::kSetStatus:
      SetStatus(scene, message.text);
      break;
    case plugin_api::Kind::kSetPageLayout:
      if (!scene->session.Supports(plugin_api::kFeatureRichPresentation) ||
          !scene->page_pending || message.value < 1 || message.value > 4 ||
          (message.value > 2 && !scene->session.Supports(plugin_api::kFeatureReadablePresentation)) ||
          message.request_id || message.flags || message.title[0] || message.text[0]) {
        scene->session.Close(); break;
      }
      scene->grid = message.value == 2 || message.value == 4;
      scene->readable = message.value >= 3; break;
    case plugin_api::Kind::kSetBackground: {
      const bool supported = scene->session.Supports(plugin_api::kFeatureBackground);
      const size_t retained = std::count_if(gPluginRuntimes.begin(), gPluginRuntimes.end(),
          [](const auto &entry) { return entry.second->background; });
      const bool allowed = supported && !scene->desktop_open && message.value <= 1 && !message.flags &&
          !message.title[0] && !message.text[0] &&
          (!message.value || scene->background || retained < 4);
      if (allowed) scene->background = message.value != 0;
      scene->session.Send(plugin_api::Kind::kBackgroundResult, message.request_id,
          allowed && scene->background ? 1 : 0, 0, nullptr,
          allowed ? "" : "Background plugin limit reached or invalid request");
      break;
    }
    case plugin_api::Kind::kSetActivityStorage:
      if (!scene->session.Supports(plugin_api::kFeatureActivities) ||
          !message.request_id || message.flags || message.value || message.title[0] ||
          (message.text[0] && message.text[0] != '/')) {
        scene->session.Close(); break;
      }
      // A storage claim cannot change while its activity owns the lease.
      for (const auto &activity : plugin_api::Activities()) {
        if (activity.plugin == scene->plugin.id && activity.Active()) {
          scene->session.Close(); return;
        }
      }
      scene->activity_id = message.request_id;
      scene->activity_storage = message.text; break;
    case plugin_api::Kind::kSetActivity: {
      if (!scene->session.Supports(plugin_api::kFeatureActivities) ||
          scene->activity_id != message.request_id) {
        scene->session.Close(); break;
      }
      bool starting = true;
      for (const auto &activity : plugin_api::Activities())
        if (activity.plugin == scene->plugin.id && activity.Active()) starting = false;
      std::string error;
      const bool accepted = plugin_api::SetActivity(scene->plugin.id,
          scene->plugin.name, scene->activity_storage, message, error);
      if (starting || !accepted)
        scene->session.Send(plugin_api::Kind::kActivityResult, message.request_id,
            accepted ? 1 : 0, 0, nullptr, error.c_str());
      if (!(message.flags & plugin_api::kActivityActive)) scene->cancel_started_ms = 0;
      break;
    }
    case plugin_api::Kind::kSetActivityFile: {
      if (!scene->session.Supports(plugin_api::kFeaturePackageReview)) {
        scene->session.Close(); break;
      }
      std::string error;
      const bool accepted = message.value == 0 && message.flags == 0 && !message.title[0] &&
          plugin_api::SetActivityFile(scene->plugin.id, message.request_id, message.text, error);
      scene->session.Send(plugin_api::Kind::kActivityFileResult, message.request_id,
          accepted ? 1 : 0, 0, nullptr, error.c_str());
      break;
    }
    case plugin_api::Kind::kReviewPackage:
      if (!scene->session.Supports(plugin_api::kFeaturePackageReview)) {
        scene->session.Close(); break;
      }
      if (scene->screen && !scene->operation_pending && !plugin_api::PluginStorageBusy() &&
          !message.value && !message.flags && !message.title[0] &&
          plugin_api::IsReviewablePackage(message.text))
        ReviewPackage(scene->screen, message.text, scene->callback, scene->context);
      else SetStatus(scene, "The downloaded ZIP is unavailable");
      break;
    case plugin_api::Kind::kRequestOperation:
      if (!scene->screen) {
        scene->session.Send(plugin_api::Kind::kOperationResult,
            message.request_id, 0, 0, nullptr, "Open the plugin before requesting a recovery operation.");
        break;
      }
      RequestOperation(scene, message);
      break;
    case plugin_api::Kind::kClose:
      scene->session.Close();
      scene->process.Stop();
      scene->stopped = true;
      SetStatus(scene, "Plugin closed");
      break;
    default:
      SetStatus(scene, "Plugin sent an invalid Host API sequence");
      scene->session.Close();
      break;
  }
}

void DetachGenericScene(GenericScene *scene) {
  scene->clipboard.Reset();
  if (scene->desktop_open) {
    ClosePluginDesktop();
    scene->desktop_open = false;
  }
  scene->screen = scene->card = scene->content = scene->status = scene->progress = nullptr;
  scene->mirror_tabs[0] = scene->mirror_tabs[1] = nullptr;
  for (auto &row : scene->rows) row.value_label = row.progress_bar = nullptr;
  if (!scene->background || scene->stopped) { delete scene; return; }
  if (scene->operation_pending) {
    scene->session.Send(plugin_api::Kind::kOperationResult,
        scene->operation_request, 0, 0, nullptr, "Plugin view closed before approval.");
    scene->operation_pending = false;
    scene->operation_request = 0;
  }
  if (scene->session.Connected()) scene->session.Send(plugin_api::Kind::kLifecycle,
      0, static_cast<uint32_t>(plugin_api::Lifecycle::kPause));
}
}  // namespace

static void PollRuntime(GenericScene *scene);

void PollGenericPluginRuntimes() {
  std::vector<GenericScene *> runtimes;
  for (const auto &entry : gPluginRuntimes) runtimes.push_back(entry.second);
  for (auto *scene : runtimes) {
    if (!scene->stopped) PollRuntime(scene);
    if (!scene->screen && (!scene->background || scene->stopped || !scene->session.Connected()))
      delete scene;
  }
}

void StopGenericPluginRuntime(const std::string &plugin) {
  auto found = gPluginRuntimes.find(plugin);
  if (found == gPluginRuntimes.end()) return;
  auto *scene = found->second;
  if (scene->session.Connected()) scene->session.Send(plugin_api::Kind::kClose);
  scene->session.Close(); scene->process.Stop(); scene->stopped = true;
  scene->background = false; scene->cancel_started_ms = 0;
  plugin_api::RemoveActivity(plugin);
  if (!scene->screen) delete scene;
  else SetStatus(scene, "Plugin stopped");
}

bool CancelGenericPluginActivity(const std::string &plugin) {
  auto found = gPluginRuntimes.find(plugin);
  if (found == gPluginRuntimes.end()) return false;
  auto *scene = found->second;
  for (const auto &activity : plugin_api::Activities()) {
    if (activity.plugin != plugin || !activity.Active() ||
        !(activity.flags & plugin_api::kActivityCancellable)) continue;
    if (!scene->session.Send(plugin_api::Kind::kActivityCancel, activity.id)) return false;
    scene->cancel_started_ms = NowMs();
    plugin_api::MarkActivityCancelling(plugin); return true;
  }
  return false;
}

void ShutdownGenericPluginRuntimes() {
  std::vector<std::string> plugins;
  for (const auto &entry : gPluginRuntimes) plugins.push_back(entry.first);
  for (const auto &plugin : plugins) StopGenericPluginRuntime(plugin);
}

void SetSelectedPluginId(const std::string &id) {
  std::lock_guard<std::mutex> lock(gSelectionMutex);
  gSelectedPlugin = id;
}

std::string GetSelectedPluginId() {
  std::lock_guard<std::mutex> lock(gSelectionMutex);
  return gSelectedPlugin;
}

void BuildGenericPluginScene(lv_obj_t *screen, const std::string &id,
                             ActionCallback callback, void *context) {
  plugins::Plugin plugin;
  if (!plugins::FindInstalled(id, plugin) || !plugins::IsGeneric(plugin)) {
    Header(screen, "Plugin unavailable",
           "The selected app is missing or does not support Host API 2.",
           callback, context);
    Navigation(screen, Action::kBackHome, callback, context, true);
    return;
  }
  GenericScene *scene = nullptr;
  auto found = gPluginRuntimes.find(id);
  if (found != gPluginRuntimes.end() && found->second->background && !found->second->stopped)
    scene = found->second;
  if (!scene) {
    if (found != gPluginRuntimes.end()) {
      auto *old = found->second;
      if (old->binding) old->binding->scene = nullptr;
      delete old;
    }
    scene = new GenericScene;
    scene->plugin = std::move(plugin);
    gPluginRuntimes[id] = scene;
  }
  if (scene->binding) scene->binding->scene = nullptr;
  scene->binding = new ViewBinding{scene};
  scene->screen = screen;
  scene->callback = callback;
  scene->context = context;
  for (auto &row : scene->rows) row.value_label = row.progress_bar = nullptr;
  lv_obj_add_event_cb(screen, [](lv_event_t *event) {
    auto *binding = static_cast<ViewBinding *>(lv_event_get_user_data(event));
    if (binding->scene) {
      auto *scene = binding->scene; scene->binding = nullptr;
      DetachGenericScene(scene);
    }
    delete binding;
  }, LV_EVENT_DELETE, scene->binding);
  Header(screen, scene->plugin.name.c_str(),
         scene->plugin.description.c_str(), callback, context);
  const bool landscape = Landscape(screen);
  const bool mirror = scene->plugin.id == "mirror";
  const int card_width = landscape ? lv_obj_get_width(screen) - 128 : 1312;
  const int top = landscape ? 320 : 430;
  if (mirror) {
    const int left_width = landscape ? (card_width - 20) / 2 : 630;
    MirrorModeCard(scene, 0, 64, top, left_width);
    MirrorModeCard(scene, 1, 64 + left_width + 20, top,
                   card_width - left_width - 20);
  }
  const int card_y = top + (mirror ? 260 : 0);
  const int card_height = lv_obj_get_height(screen) - card_y - 36;
  scene->card = lv_obj_create(screen);
  Panel(scene->card, 42, kMainPanel);
  lv_obj_add_event_cb(scene->card, [](lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CANCEL) return;
    auto *scene = static_cast<GenericScene *>(lv_event_get_user_data(event));
    RequestPage(scene, scene->page_back_action);
  }, LV_EVENT_CANCEL, scene);
  lv_obj_set_pos(scene->card, 64, card_y);
  lv_obj_set_size(scene->card, card_width, card_height);
  scene->content = lv_obj_create(scene->card);
  Clear(scene->content);
  lv_obj_set_pos(scene->content, 24, 24);
  lv_obj_set_size(scene->content, card_width - 48, card_height - 132);
  lv_obj_add_flag(scene->content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(scene->content, LV_DIR_VER);
  if (!scene->launched) {
    scene->page_title = "Preparing " + scene->plugin.name;
    scene->page_body = "Verifying the plugin runtime and expanding it into private RAM.";
  }
  RenderPage(scene);
  SetBackAction(scene, scene->page_back_action);
  scene->progress = lv_bar_create(scene->card);
  lv_obj_set_pos(scene->progress, 36, card_height - 80);
  lv_obj_set_size(scene->progress, card_width - 72, 10);
  lv_obj_set_style_bg_color(scene->progress, kAccent, LV_PART_INDICATOR);
  scene->status = Label(scene->card, "Checking signed runtime",
                        &lv_font_montserrat_24, kMuted);
  lv_obj_align(scene->status, LV_ALIGN_BOTTOM_LEFT, 36, -24);
  Navigation(screen, Action::kBackHome, callback, context, true);

  if (!scene->worker.joinable()) scene->worker = std::thread([scene] {
    web::PreparePluginRuntime(scene->preparation, scene->plugin.id.c_str(),
                              scene->plugin.type.c_str(),
                              scene->plugin.entry.c_str());
  });
  if (scene->launched) {
    lv_bar_set_value(scene->progress, 100, LV_ANIM_OFF);
    SetStatus(scene, scene->last_status);
    if (scene->session.Connected()) scene->session.Send(plugin_api::Kind::kLifecycle,
        0, static_cast<uint32_t>(plugin_api::Lifecycle::kResume));
  }
}

static void PollRuntime(GenericScene *scene) {
    if (!scene->preparation.done.load(std::memory_order_acquire)) {
      if (scene->progress) lv_bar_set_value(scene->progress, scene->preparation.progress.load(),
                       LV_ANIM_OFF);
      return;
    }
    if (!scene->preparation.verified) {
      SetStatus(scene, scene->preparation.error);
      return;
    }
    if (!scene->launched) {
      scene->launched = true;
      scene->launched_ms = NowMs();
      int control = -1;
      std::string error;
      if (!scene->process.Start(scene->preparation.directory, control, error) ||
          !scene->session.Adopt(control)) {
        scene->stopped = true;
        SetStatus(scene, error.empty() ? scene->session.Status() : error);
        return;
      }
      if (scene->progress) lv_bar_set_value(scene->progress, 100, LV_ANIM_ON);
    }
    for (const auto &message : scene->session.Poll())
      HandleMessage(scene, message);
    if (!scene->session.Connected()) {
      scene->clipboard.Reset();
      scene->process.Stop(); scene->stopped = true;
      plugin_api::RemoveActivity(scene->plugin.id);
      SetStatus(scene, scene->session.Status()); return;
    }
    if (scene->plugin.id == "mirror" && !scene->page_pending &&
        NowMs() - scene->mirror_code_checked_ms >= 500) {
      scene->mirror_code_checked_ms = NowMs();
      const std::string code = plugin_api::ActiveMirrorMode() ==
          plugin_api::MirrorMode::kOff ? "" : aera::remote::AccessCode();
      if (code != scene->mirror_access_code) RenderPage(scene);
    }
    if (scene->session.Connected() && !scene->session.Negotiated() &&
        NowMs() - scene->launched_ms > 5000) {
      scene->session.Close();
      SetStatus(scene, "Plugin handshake timed out");
    }
    if (scene->operation_pending &&
        NowMs() - scene->operation_started_ms > 60000) {
      scene->session.Send(plugin_api::Kind::kOperationResult,
                          scene->operation_request, 0, 0, nullptr,
                          "Host operation approval timed out.");
      scene->operation_pending = false;
      scene->operation_request = 0;
    }
    if (scene->operation_pending && (!scene->screen || !ModalVisible(scene->screen))) {
      scene->session.Send(plugin_api::Kind::kOperationResult,
                          scene->operation_request, 0, 0, nullptr,
                          "Host operation cancelled by the user.");
      scene->operation_pending = false;
      scene->operation_request = 0;
      SetStatus(scene, "Operation cancelled");
    }
    if (!scene->stopped && !scene->process.Running()) {
      scene->stopped = true;
      scene->session.Close();
      plugin_api::RemoveActivity(scene->plugin.id);
      SetStatus(scene, "Plugin process exited; its resources were reclaimed");
    }
}

}  // namespace aeraui
