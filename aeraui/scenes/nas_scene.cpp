/*
 * Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "scene.hpp"

#include <algorithm>
#include <atomic>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "nas/smb_browser.hpp"
#include "phone_keyboard.hpp"
#include "ui_components.hpp"

namespace aeraui {
namespace {
using namespace design;
using namespace widgets;

NasRequest gRequest;

enum class Field { kHost, kPort, kUser, kPassword, kShare, kPath, kDomain, kWifiNetwork };
struct FolderPicker;

struct NasUi {
  NasScene scene;
  lv_obj_t *screen = nullptr;
  lv_obj_t *hero = nullptr;
  lv_obj_t *status_icon = nullptr;
  lv_obj_t *activity = nullptr;
  lv_timer_t *timer = nullptr;
  ActionCallback callback = nullptr;
  void *context = nullptr;
  NasStatus snapshot;
  NasOperation running = NasOperation::kMountAndUse;
  bool busy = false;
  bool activity_animating = false;
  uint32_t busy_phase = 0;
  bool advanced = false;
  FolderPicker *picker = nullptr;
};

struct BrowseWork {
  std::atomic<bool> cancel{false};
  std::atomic<bool> done{false};
  smb::Result result;
};

struct FolderPicker {
  NasUi *owner = nullptr;
  lv_obj_t *overlay = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *path_label = nullptr;
  lv_obj_t *list = nullptr;
  lv_obj_t *select = nullptr;
  lv_obj_t *back = nullptr;
  lv_timer_t *timer = nullptr;
  std::string share;
  std::string path;
  bool valid = false;
  std::shared_ptr<BrowseWork> work;
};

const char *FieldTitle(Field field) {
  switch (field) {
    case Field::kHost: return "Host or IP address";
    case Field::kPort: return "SFTP port";
    case Field::kUser: return "Username";
    case Field::kPassword: return "Password";
    case Field::kShare: return "SMB share name";
    case Field::kPath: return "Remote folder or path";
    case Field::kDomain: return "SMB domain or workgroup";
    case Field::kWifiNetwork: return "Wi-Fi network";
  }
  return "Network storage";
}

std::string FieldValue(const NasConfig &config, Field field) {
  switch (field) {
    case Field::kHost: return config.host;
    case Field::kPort: return config.port;
    case Field::kUser: return config.user;
    case Field::kPassword: return config.password;
    case Field::kShare: return config.share;
    case Field::kPath: return config.path;
    case Field::kDomain: return config.domain;
    case Field::kWifiNetwork: return config.wifi_ssid;
  }
  return {};
}

void SetField(NasConfig *config, Field field, const std::string &value) {
  if (config == nullptr) return;
  switch (field) {
    case Field::kHost: config->host = value; break;
    case Field::kPort: config->port = value; break;
    case Field::kUser: config->user = value; break;
    case Field::kPassword: config->password = value; break;
    case Field::kShare: config->share = value; break;
    case Field::kPath: config->path = value; break;
    case Field::kDomain: config->domain = value; break;
    case Field::kWifiNetwork: config->wifi_ssid = value; break;
  }
}

uint32_t FieldLimit(Field field) {
  switch (field) {
    case Field::kPassword: return 254;
    case Field::kPath: return 256;
    case Field::kPort: return 5;
    case Field::kWifiNetwork: return 32;
    default: return 128;
  }
}

void Populate(NasUi *state);
void DeferredPopulate(void *state) { Populate(static_cast<NasUi *>(state)); }
void OpenBrowser(NasUi *state, const std::string &share = {},
                 const std::string &path = {});
void ChooseWifiNetwork(NasUi *state);
void ChoosePrivateKey(NasUi *state, const std::string &path);

bool SaveConfig(NasUi *state, const NasConfig &config) {
  std::string error;
  if (!RecoverySetNasConfig(config, &error)) {
    Sheet(state->screen, "Invalid network storage setting",
          error.empty() ? "The setting could not be saved." : error);
    return false;
  }
  state->snapshot = RecoveryNasStatus();
  return true;
}

void Close(lv_obj_t *overlay) { lv_obj_delete_async(overlay); }

void SetActivity(NasUi *state, bool active) {
  if (state == nullptr || state->activity == nullptr) return;
  if (active && !state->activity_animating) {
    state->activity_animating = true;
    lv_obj_remove_flag(state->activity, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t spin;
    lv_anim_init(&spin);
    lv_anim_set_var(&spin, state->activity);
    lv_anim_set_values(&spin, 0, 3600);
    lv_anim_set_duration(&spin, 900);
    lv_anim_set_repeat_count(&spin, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&spin, [](void *target, int32_t angle) {
      lv_obj_set_style_transform_rotation(static_cast<lv_obj_t *>(target),
                                          angle, 0);
    });
    lv_anim_start(&spin);
  } else if (!active && state->activity_animating) {
    state->activity_animating = false;
    lv_anim_delete(state->activity, nullptr);
    lv_obj_set_style_transform_rotation(state->activity, 0, 0);
    lv_obj_add_flag(state->activity, LV_OBJ_FLAG_HIDDEN);
  }
}

std::string BusyTitle(const NasUi *state) {
  const char *base = "Updating storage";
  switch (state->running) {
    case NasOperation::kMount:
    case NasOperation::kMountAndUse: base = "Mounting storage"; break;
    case NasOperation::kUse: base = "Selecting storage"; break;
    case NasOperation::kUnmount: base = "Unmounting storage"; break;
  }
  std::string title(base);
  title.append(1 + (state->busy_phase % 3), '.');
  return title;
}

void EditField(NasUi *state, Field field, bool browse_after = false) {
  if (state->busy) return;
  if (state->snapshot.mounted) {
    Sheet(state->screen, "Network storage is mounted",
          "Unmount it before changing its connection settings.");
    return;
  }

  auto *overlay = lv_obj_create(state->screen);
  lv_obj_set_user_data(overlay, &kModalMarker);
  Clear(overlay);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);

  auto *sheet = lv_obj_create(overlay);
  Panel(sheet, 48, kMainSheet);
  lv_obj_set_size(sheet, 1312, 1260);
  lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, -64);
  lv_obj_set_style_pad_all(sheet, 48, 0);
  auto *title = Label(sheet, field == Field::kPassword && state->snapshot.config.type == "sftp" &&
      state->snapshot.config.auth == "key"
      ? "Key passphrase" : FieldTitle(field), &lv_font_montserrat_48, kText);
  lv_obj_set_width(title, 1180);
  auto *hint = Label(sheet,
      field == Field::kPassword ? "The value is hidden while you type."
      : field == Field::kPath ? "SFTP may use an absolute path; SMB uses a path inside the share."
      : "Saved securely for AERA network storage.",
      &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(hint, 0, 76);
  lv_obj_set_width(hint, 1180);

  auto *input = TextArea(sheet);
  lv_obj_set_pos(input, 0, 150);
  lv_obj_set_size(input, 1216, 126);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_max_length(input, FieldLimit(field));
  lv_textarea_set_password_mode(input, field == Field::kPassword);
  lv_textarea_set_text(input, FieldValue(state->snapshot.config, field).c_str());
  lv_obj_set_style_text_font(input, UiFont(&lv_font_montserrat_32), 0);
  lv_obj_set_style_text_color(input, kText, 0);
  lv_obj_set_style_bg_color(input, kMainPanel, 0);
  lv_obj_set_style_bg_opa(input, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(input, kAccent, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(input, 2, LV_STATE_FOCUSED);
  lv_obj_set_style_radius(input, 24, 0);
  lv_obj_set_style_pad_all(input, 28, 0);

  auto *keyboard = lv_keyboard_create(sheet);
  phone_keyboard::Apply(keyboard);
  lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(keyboard, 0, 306);
  lv_obj_set_size(keyboard, 1216, 630);
  lv_keyboard_set_mode(keyboard, field == Field::kPort
                                     ? LV_KEYBOARD_MODE_NUMBER
                                     : LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(keyboard, input);

  auto *cancel = Button(sheet, "Cancel", [overlay] { Close(overlay); });
  lv_obj_set_pos(cancel, 0, 986);
  lv_obj_set_size(cancel, 580, 116);
  auto save_value = [state, field, input, overlay, browse_after] {
    NasConfig config = state->snapshot.config;
    SetField(&config, field, lv_textarea_get_text(input));
    if (!SaveConfig(state, config)) return;
    Close(overlay);
    Populate(state);
    if (browse_after) OpenBrowser(state, config.share);
  };
  auto *save = Button(sheet, "Save", save_value, true);
  lv_obj_set_pos(save, 636, 986);
  lv_obj_set_size(save, 580, 116);
  auto *ready = new Handler(save_value);
  lv_obj_add_event_cb(input, [](lv_event_t *event) {
    auto *ready = static_cast<Handler *>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_DELETE) delete ready;
    else if (lv_event_get_code(event) == LV_EVENT_READY) (*ready)();
  }, LV_EVENT_ALL, ready);
  lv_obj_send_event(input, LV_EVENT_CLICKED, nullptr);
  AnimateEnter(sheet, 0, 42);
}

void DismissPicker(FolderPicker *picker) {
  if (picker->work) picker->work->cancel.store(true);
  if (picker->owner) picker->owner->picker = nullptr;
  picker->owner = nullptr;
  Close(picker->overlay);
}

void LoadFolder(FolderPicker *picker) {
  if (picker->work) picker->work->cancel.store(true);
  picker->valid = false;
  lv_obj_add_state(picker->select, LV_STATE_DISABLED);
  if (picker->share.empty()) lv_obj_add_state(picker->back, LV_STATE_DISABLED);
  else lv_obj_remove_state(picker->back, LV_STATE_DISABLED);
  i18n::BindLabel(picker->title, picker->share.empty() ? "SMB shares" : "Folder");
  const NasConfig config = picker->owner->snapshot.config;
  const std::string location = "//" + config.host +
      (picker->share.empty() ? "" : "/" + picker->share) +
      (picker->path.empty() ? "" : "/" + picker->path);
  lv_label_set_text(picker->path_label, location.c_str());
  lv_obj_clean(picker->list);
  lv_obj_scroll_to_y(picker->list, 0, LV_ANIM_OFF);
  auto *loading = Label(picker->list, "Loading folders...", &lv_font_montserrat_32, kMuted);
  lv_obj_set_width(loading, LV_PCT(100));
  auto work = std::make_shared<BrowseWork>();
  picker->work = work;
  const std::string share = picker->share;
  const std::string path = picker->path;
  std::thread([work, config, share, path] {
    work->result = smb::List(config, share, path, work->cancel);
    work->done.store(true, std::memory_order_release);
  }).detach();
}

void PopulateFolders(FolderPicker *picker) {
  lv_obj_clean(picker->list);
  const auto &result = picker->work->result;
  picker->valid = result.error.empty();
  if (!picker->valid) {
    auto *error = Label(picker->list, "Network storage action failed", &lv_font_montserrat_32, kText);
    lv_obj_set_width(error, LV_PCT(100));
    auto *detail = Label(picker->list, "", &lv_font_montserrat_24, kMuted);
    lv_label_set_text(detail, result.error.c_str());
    lv_obj_set_pos(detail, 0, 110);
    lv_obj_set_width(detail, LV_PCT(100));
    return;
  }
  if (!picker->share.empty()) lv_obj_remove_state(picker->select, LV_STATE_DISABLED);
  if (result.directories.empty()) {
    auto *empty = Label(picker->list, picker->share.empty() ? "No shares found" : "No folders found",
                        &lv_font_montserrat_32, kMuted);
    lv_obj_set_width(empty, LV_PCT(100));
    return;
  }
  int y = 0;
  const int height = std::max(144, 2 * static_cast<int>(UiFont(&lv_font_montserrat_32)->line_height) + 36);
  for (const auto &name : result.directories) {
    auto *row = Button(picker->list, "", [picker, name] {
      if (picker->share.empty()) picker->share = name;
      else picker->path += (picker->path.empty() ? "" : "/") + name;
      LoadFolder(picker);
    });
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_style_transform_scale(row, 256, LV_STATE_PRESSED);
    auto *icon = Label(row, picker->share.empty() ? LV_SYMBOL_DRIVE : LV_SYMBOL_DIRECTORY,
                       &lv_font_montserrat_32, kAccent);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 24, 0);
    auto *label = Label(row, "", &lv_font_montserrat_32, kText);
    lv_label_set_text(label, name.c_str());
    lv_obj_set_pos(label, 108, 18);
    lv_obj_set_width(label, lv_obj_get_width(picker->list) - 190);
    lv_obj_set_height(label, height - 36);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    auto *arrow = Label(row, LV_SYMBOL_RIGHT, &lv_font_montserrat_24, kMuted);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -24, 0);
    y += height + 12;
  }
}

void ParentFolder(FolderPicker *picker) {
  if (picker->share.empty()) { DismissPicker(picker); return; }
  if (picker->path.empty()) picker->share.clear();
  else {
    const auto slash = picker->path.rfind('/');
    picker->path = slash == std::string::npos ? "" : picker->path.substr(0, slash);
  }
  LoadFolder(picker);
}

void OpenBrowser(NasUi *state, const std::string &share, const std::string &path) {
  if (state->busy || state->picker) return;
  if (state->snapshot.mounted) {
    Sheet(state->screen, "Network storage is mounted",
          "Unmount it before changing its connection settings.");
    return;
  }
  if (state->snapshot.config.host.empty()) {
    Sheet(state->screen, "Host required",
          "Enter the SFTP or SMB server hostname/IP before mounting.");
    return;
  }
  auto *picker = new FolderPicker;
  picker->owner = state;
  picker->share = share;
  picker->path = path;
  state->picker = picker;
  picker->overlay = lv_obj_create(state->screen);
  lv_obj_set_user_data(picker->overlay, &kPersistentModalMarker);
  Clear(picker->overlay);
  lv_obj_set_size(picker->overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(picker->overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(picker->overlay, LV_OPA_60, 0);
  lv_obj_add_flag(picker->overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(picker->overlay, [](lv_event_t *event) {
    auto *picker = static_cast<FolderPicker *>(lv_event_get_user_data(event));
    if (lv_event_get_target_obj(event) == lv_event_get_current_target_obj(event))
      DismissPicker(picker);
  }, LV_EVENT_CLICKED, picker);
  lv_obj_add_event_cb(picker->overlay, [](lv_event_t *event) {
    ParentFolder(static_cast<FolderPicker *>(lv_event_get_user_data(event)));
  }, LV_EVENT_CANCEL, picker);
  lv_obj_add_event_cb(picker->overlay, [](lv_event_t *event) {
    auto *picker = static_cast<FolderPicker *>(lv_event_get_user_data(event));
    if (picker->work) picker->work->cancel.store(true);
    if (picker->timer) lv_timer_delete(picker->timer);
    if (picker->owner) picker->owner->picker = nullptr;
    delete picker;
  }, LV_EVENT_DELETE, picker);

  auto *sheet = lv_obj_create(picker->overlay);
  Panel(sheet, 48, kMainSheet);
  lv_obj_update_layout(state->screen);
  const int width = std::min(1500, static_cast<int>(lv_obj_get_width(state->screen)) - 128);
  const int height = std::min(2320, static_cast<int>(lv_obj_get_height(state->screen)) - 400);
  lv_obj_set_size(sheet, width, height);
  lv_obj_center(sheet);
  lv_obj_set_style_pad_all(sheet, 40, 0);
  picker->title = Label(sheet, "SMB shares", &lv_font_montserrat_48, kText);
  lv_obj_set_width(picker->title, width - 520);
  lv_label_set_long_mode(picker->title, LV_LABEL_LONG_DOT);
  picker->path_label = Label(sheet, "", &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(picker->path_label, 0, 110);
  lv_obj_set_size(picker->path_label, width - 80, 110);
  lv_label_set_long_mode(picker->path_label, LV_LABEL_LONG_DOT);
  auto *close = Button(sheet, LV_SYMBOL_CLOSE, [picker] { DismissPicker(picker); });
  lv_obj_set_pos(close, width - 180, 0);
  lv_obj_set_size(close, 100, 88);
  auto *refresh = Button(sheet, LV_SYMBOL_REFRESH, [picker] { LoadFolder(picker); });
  lv_obj_set_pos(refresh, width - 300, 0);
  lv_obj_set_size(refresh, 100, 88);
  picker->back = Button(sheet, LV_SYMBOL_UP, [picker] {
    ParentFolder(picker);
  });
  lv_obj_set_pos(picker->back, width - 420, 0);
  lv_obj_set_size(picker->back, 100, 88);
  picker->list = Scroll(sheet, 250, height - 500);
  lv_obj_set_x(picker->list, 0);
  lv_obj_set_width(picker->list, width - 80);
  lv_obj_set_scrollbar_mode(picker->list, LV_SCROLLBAR_MODE_ON);
  lv_obj_update_layout(picker->list);

  auto *manual = Button(sheet, "SMB share name", [picker] {
    auto *owner = picker->owner;
    DismissPicker(picker);
    EditField(owner, Field::kShare, true);
  });
  lv_obj_set_pos(manual, 0, height - 190);
  lv_obj_set_size(manual, (width - 100) / 2, 110);
  picker->select = Button(sheet, "Use this folder", [picker] {
    if (!picker->valid || picker->share.empty()) return;
    auto *owner = picker->owner;
    NasConfig config = owner->snapshot.config;
    config.share = picker->share;
    config.path = picker->path;
    if (!SaveConfig(owner, config)) return;
    DismissPicker(picker);
    Populate(owner);
  }, true);
  lv_obj_set_pos(picker->select, (width - 100) / 2 + 20, height - 190);
  lv_obj_set_size(picker->select, (width - 100) / 2, 110);
  picker->timer = lv_timer_create([](lv_timer_t *timer) {
    auto *picker = static_cast<FolderPicker *>(lv_timer_get_user_data(timer));
    if (picker->work && picker->work->done.load(std::memory_order_acquire)) {
      PopulateFolders(picker);
      picker->work.reset();
    }
  }, 80, picker);
  LoadFolder(picker);
  AnimateEnter(sheet, 0, 18);
}

void Dispatch(NasUi *state, NasOperation operation) {
  if (state == nullptr || state->busy) return;
  const auto &config = state->snapshot.config;
  if (operation == NasOperation::kMountAndUse && config.host.empty()) {
    Sheet(state->screen, "Host required",
          "Enter the SFTP or SMB server hostname/IP before mounting.");
    return;
  }
  gRequest.operation = operation;
  state->callback(Action::kRunNasOperation, state->context);
}

void AddField(NasUi *state, int x, int y, Field field,
              lv_obj_t *parent = nullptr, int width = 640) {
  if (parent == nullptr) parent = state->scene.list;
  std::string value = FieldValue(state->snapshot.config, field);
  if (field == Field::kPassword)
    value = value.empty() ? "Not set" : "Password saved";
  else if (value.empty())
    value = "Not set";
  auto *card = lv_button_create(parent);
  Panel(card, 30, kMainSheet);
  Interactive(card, kMainSelected);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, 160);
  lv_obj_set_style_transform_scale(card, 256, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, kMainLine, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
  OnClick(card, [state, field] { EditField(state, field); });
  auto *name = Label(card, field == Field::kPassword && state->snapshot.config.type == "sftp" &&
      state->snapshot.config.auth == "key"
      ? "Key passphrase" : FieldTitle(field), &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(name, 30, 26);
  auto *copy = Label(card, value.c_str(), &lv_font_montserrat_32, kText);
  lv_obj_set_pos(copy, 30, 78);
  lv_obj_set_width(copy, width - 120);
  lv_label_set_long_mode(copy, LV_LABEL_LONG_DOT);
  auto *edit = Label(card, LV_SYMBOL_RIGHT, &lv_font_montserrat_24,
                     kMutedStrong);
  lv_obj_align(edit, LV_ALIGN_RIGHT_MID, -28, 0);
  AnimateEnter(card, 20 + static_cast<uint32_t>(y / 10), 9);
}

void SelectProtocol(NasUi *state, const char *type) {
  if (state->snapshot.mounted) {
    Sheet(state->screen, "Network storage is mounted",
          "Unmount it before switching protocols.");
    return;
  }
  if (state->snapshot.config.type == type) return;
  NasConfig config = RecoverySavedNasConfig(type);
  if (SaveConfig(state, config)) Populate(state);
}

lv_obj_t *ProtocolButton(NasUi *state, lv_obj_t *parent, int x,
                         const char *type, const char *title) {
  const bool selected = state->snapshot.config.type == type;
  auto *button = lv_button_create(parent);
  Panel(button, 28, kMainPanel);
  Interactive(button, kMainSelected);
  lv_obj_set_pos(button, x, 28);
  lv_obj_set_size(button, 282, 112);
  lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
  lv_obj_set_style_border_color(button, selected ? kAccent : kMainLine, 0);
  lv_obj_set_style_border_opa(button, selected ? LV_OPA_70 : LV_OPA_30, 0);
  auto *label = Label(button, title, &lv_font_montserrat_32,
                      selected ? kAccent : kText);
  lv_obj_center(label);
  OnClick(button, [state, type] { SelectProtocol(state, type); });
  return button;
}

lv_obj_t *AuthenticationButton(NasUi *state, lv_obj_t *parent, int x,
                               const char *auth, const char *title) {
  const bool selected = state->snapshot.config.auth == auth;
  auto *button = lv_button_create(parent);
  Panel(button, 28, kMainPanel);
  Interactive(button, kMainSelected);
  lv_obj_set_pos(button, x, 28);
  lv_obj_set_size(button, 282, 112);
  lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
  lv_obj_set_style_border_color(button, selected ? kAccent : kMainLine, 0);
  lv_obj_set_style_border_opa(button, selected ? LV_OPA_70 : LV_OPA_30, 0);
  auto *label = Label(button, title, &lv_font_montserrat_28,
                      selected ? kAccent : kText);
  lv_obj_center(label);
  OnClick(button, [state, auth] {
    if (state->snapshot.mounted || state->busy ||
        state->snapshot.config.auth == auth) return;
    auto config = state->snapshot.config;
    config.auth = auth;
    config.password.clear();
    if (SaveConfig(state, config)) Populate(state);
  });
  return button;
}

void StyleSwitch(lv_obj_t *toggle, bool enabled);

int AddAutoConnectCard(NasUi *state, int y) {
  const bool expanded = state->snapshot.config.auto_connect;
  const int height = expanded ? 340 : 168;
  auto *card = lv_obj_create(state->scene.list);
  Panel(card, 32, kMainSheet);
  lv_obj_set_pos(card, 0, y);
  lv_obj_set_size(card, 1312, height);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, kMainLine, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_30, 0);

  auto *header = lv_button_create(card);
  Clear(header);
  Interactive(header, kMainSelected);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, 1312, 168);
  lv_obj_set_style_transform_scale(header, 256, LV_STATE_PRESSED);
  auto *title = Label(header, "Auto-connect", &lv_font_montserrat_32, kText);
  lv_obj_set_pos(title, 32, 34);
  auto *copy = Label(header, "Connect Wi-Fi before mounting network storage",
                     &lv_font_montserrat_20, kMuted);
  lv_obj_set_pos(copy, 32, 94);
  auto *toggle = lv_switch_create(header);
  StyleSwitch(toggle, expanded);
  lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -32, 0);
  OnClick(header, [state] {
    if (state->busy) return;
    auto config = state->snapshot.config;
    const bool enabled = !config.auto_connect;
    if (enabled && config.wifi_ssid.empty()) {
      const auto wifi = RecoveryWifiConnection();
      if (wifi.connected) config.wifi_ssid = wifi.ssid;
    }
    config.auto_connect = enabled;
    if (SaveConfig(state, config)) Populate(state);
  });

  if (expanded) {
    auto *network = lv_button_create(card);
    Panel(network, 26, kMainPanel);
    Interactive(network, kMainSelected);
    lv_obj_set_pos(network, 28, 184);
    lv_obj_set_size(network, 1256, 128);
    lv_obj_set_style_transform_scale(network, 256, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(network, 1, 0);
    lv_obj_set_style_border_color(network, kMainLine, 0);
    lv_obj_set_style_border_opa(network, LV_OPA_30, 0);
    OnClick(network, [state] { ChooseWifiNetwork(state); });
    auto *plate = lv_obj_create(network);
    Panel(plate, 18, kMainSelected);
    lv_obj_set_pos(plate, 22, 20);
    lv_obj_set_size(plate, 88, 88);
    auto *icon = Label(plate, LV_SYMBOL_WIFI, &lv_font_montserrat_36, kAccent);
    lv_obj_center(icon);
    auto *name = Label(network, "Wi-Fi network", &lv_font_montserrat_28, kText);
    lv_obj_set_pos(name, 136, 18);
    auto *ssid = Label(network,
        state->snapshot.config.wifi_ssid.empty()
            ? "Choose a saved network or enter an SSID"
            : state->snapshot.config.wifi_ssid.c_str(),
        &lv_font_montserrat_20, kMuted);
    lv_obj_set_pos(ssid, 136, 72);
    lv_obj_set_width(ssid, 980);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
    auto *arrow = Label(network, LV_SYMBOL_RIGHT, &lv_font_montserrat_28,
                        kMutedStrong);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -28, 0);
  }
  AnimateEnter(card, 20 + static_cast<uint32_t>(y / 10), 9);
  return height;
}

int AddAuthenticationCard(NasUi *state, int y) {
  const bool expanded = state->snapshot.config.auth == "key";
  const int height = expanded ? 340 : 168;
  auto *card = lv_obj_create(state->scene.list);
  Panel(card, 32, kMainSheet);
  lv_obj_set_pos(card, 0, y);
  lv_obj_set_size(card, 1312, height);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, kMainLine, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
  auto *title = Label(card, "Authentication", &lv_font_montserrat_32, kText);
  lv_obj_set_pos(title, 32, 34);
  auto *copy = Label(card, "Choose how AERA signs in to your SFTP server",
                     &lv_font_montserrat_20, kMuted);
  lv_obj_set_pos(copy, 32, 94);
  AuthenticationButton(state, card, 696, "password", "Password");
  AuthenticationButton(state, card, 994, "key", "Private key");
  if (expanded) {
    auto *key = lv_button_create(card);
    Panel(key, 26, kMainPanel);
    Interactive(key, kMainSelected);
    lv_obj_set_pos(key, 28, 184);
    lv_obj_set_size(key, 1256, 128);
    lv_obj_set_style_transform_scale(key, 256, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(key, 1, 0);
    lv_obj_set_style_border_color(key, kMainLine, 0);
    lv_obj_set_style_border_opa(key, LV_OPA_30, 0);
    OnClick(key, [state] { ChoosePrivateKey(state, {}); });
    auto *plate = lv_obj_create(key);
    Panel(plate, 18, kMainSelected);
    lv_obj_set_pos(plate, 22, 20);
    lv_obj_set_size(plate, 88, 88);
    auto *icon = Label(plate, LV_SYMBOL_FILE, &lv_font_montserrat_36, kAccent);
    lv_obj_center(icon);
    auto *name = Label(key, "Private key", &lv_font_montserrat_28, kText);
    lv_obj_set_pos(name, 136, 18);
    const std::string value = state->snapshot.key_available
        ? (state->snapshot.config.key_name.empty()
            ? "Private key saved" : state->snapshot.config.key_name)
        : "Choose a private key";
    auto *selected = Label(key, value.c_str(), &lv_font_montserrat_20, kMuted);
    lv_obj_set_pos(selected, 136, 72);
    lv_obj_set_width(selected, 980);
    lv_label_set_long_mode(selected, LV_LABEL_LONG_DOT);
    auto *arrow = Label(key, LV_SYMBOL_RIGHT, &lv_font_montserrat_28,
                        kMutedStrong);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -28, 0);
  }
  AnimateEnter(card, 20 + static_cast<uint32_t>(y / 10), 9);
  return height;
}

void AddBrowseFoldersCard(NasUi *state, int y) {
  const auto &config = state->snapshot.config;
  const std::string target = config.share +
      (config.path.empty() ? "" : "/" + config.path);
  auto *card = lv_button_create(state->scene.list);
  Panel(card, 30, kMainPanel);
  Interactive(card, kMainSelected);
  lv_obj_set_pos(card, 0, y);
  lv_obj_set_size(card, 1312, 160);
  lv_obj_set_style_transform_scale(card, 256, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, kMainLine, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
  OnClick(card, [state] { OpenBrowser(state); });
  auto *plate = lv_obj_create(card);
  Panel(plate, 22, kMainSelected);
  lv_obj_set_pos(plate, 28, 30);
  lv_obj_set_size(plate, 100, 100);
  auto *icon = Label(plate, LV_SYMBOL_DIRECTORY, &lv_font_montserrat_40,
                     kAccent);
  lv_obj_center(icon);
  auto *name = Label(card, "Browse folders", &lv_font_montserrat_32, kText);
  lv_obj_set_pos(name, 158, 28);
  auto *copy = Label(card, target.c_str(), &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(copy, 158, 90);
  lv_obj_set_width(copy, 1020);
  lv_label_set_long_mode(copy, LV_LABEL_LONG_DOT);
  auto *arrow = Label(card, LV_SYMBOL_RIGHT, &lv_font_montserrat_28,
                      kMutedStrong);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -32, 0);
  AnimateEnter(card, 20 + static_cast<uint32_t>(y / 10), 9);
}

int AddAdvancedSmbCard(NasUi *state, int y) {
  const int height = state->advanced ? 552 : 168;
  auto *card = lv_obj_create(state->scene.list);
  Panel(card, 32, kMainSheet);
  lv_obj_set_pos(card, 0, y);
  lv_obj_set_size(card, 1312, height);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, kMainLine, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_30, 0);

  auto *header = lv_button_create(card);
  Clear(header);
  Interactive(header, kMainSelected);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, 1312, 168);
  lv_obj_set_style_transform_scale(header, 256, LV_STATE_PRESSED);
  auto *title = Label(header, "Advanced", &lv_font_montserrat_32, kText);
  lv_obj_set_pos(title, 32, 34);
  auto *copy = Label(header, "Share, remote path and domain settings",
                     &lv_font_montserrat_20, kMuted);
  lv_obj_set_pos(copy, 32, 94);
  auto *arrow = Label(header, state->advanced ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                      &lv_font_montserrat_28, kMutedStrong);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -32, 0);
  OnClick(header, [state] {
    if (state->busy) return;
    state->advanced = !state->advanced;
    Populate(state);
  });

  if (state->advanced) {
    AddField(state, 28, 184, Field::kShare, card, 618);
    AddField(state, 666, 184, Field::kPath, card, 618);
    AddField(state, 28, 364, Field::kDomain, card, 1256);
  }
  AnimateEnter(card, 20 + static_cast<uint32_t>(y / 10), 9);
  return height;
}

void StyleSwitch(lv_obj_t *toggle, bool enabled) {
  lv_obj_set_size(toggle, 108, 60);
  lv_obj_remove_flag(toggle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(toggle, kMainLine, LV_PART_MAIN);
  lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER,
                          LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(toggle, kAccent,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(toggle, kText, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(toggle, -8, LV_PART_KNOB);
  if (enabled)
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
  else
    lv_obj_remove_state(toggle, LV_STATE_CHECKED);
}

void AddCacheCard(NasUi *state, int x, int y) {
  const bool enabled = state->snapshot.config.cache_mode == "data";
  auto *card = lv_button_create(state->scene.list);
  Panel(card, 30, kMainPanel);
  Interactive(card, kMainSelected);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, 640, 160);
  lv_obj_set_style_transform_scale(card, 256, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, kMainLine, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
  OnClick(card, [state] {
    if (state->snapshot.mounted) {
      Sheet(state->screen, "Network storage is mounted",
            "Unmount it before changing cache mode.");
      return;
    }
    NasConfig config = state->snapshot.config;
    config.cache_mode = config.cache_mode == "data" ? "off" : "data";
    if (SaveConfig(state, config)) Populate(state);
  });
  auto *name = Label(card, "Write cache", &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(name, 30, 26);
  auto *copy = Label(card, enabled ? "Write-back enabled" : "Off",
                     &lv_font_montserrat_32, kText);
  lv_obj_set_pos(copy, 30, 78);
  auto *toggle = lv_switch_create(card);
  StyleSwitch(toggle, enabled);
  lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -30, 0);
  AnimateEnter(card, 20 + static_cast<uint32_t>(y / 10), 9);
}

void ChoosePrivateKey(NasUi *state, const std::string &path = {}) {
  if (state->busy || state->snapshot.mounted) return;
  auto *overlay = lv_obj_create(state->screen);
  lv_obj_set_user_data(overlay, &kModalMarker);
  Clear(overlay);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  MainBackground(overlay);
  auto *title = Label(overlay, "Select a private key", &lv_font_montserrat_48, kText);
  lv_obj_set_pos(title, 80, 150);
  auto *where = Label(overlay, path.empty() ? "Choose storage" : path.c_str(),
                       &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(where, 80, 230);
  lv_obj_set_width(where, lv_obj_get_width(state->screen) - 160);
  lv_label_set_long_mode(where, LV_LABEL_LONG_DOT);
  auto *list = lv_obj_create(overlay);
  Clear(list);
  lv_obj_set_pos(list, 64, 300);
  lv_obj_set_size(list, lv_obj_get_width(state->screen) - 128,
                       lv_obj_get_height(state->screen) - 510);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  struct Entry { std::string name, path; bool directory; };
  std::vector<Entry> entries;
  if (path.empty()) {
    for (const auto &storage : RecoveryBackupStorages())
      if (storage.mounted) entries.push_back({storage.name, storage.path, true});
  } else {
    std::string parent;
    bool storage_root = false;
    for (const auto &storage : RecoveryBackupStorages())
      if (storage.path == path) storage_root = true;
    const auto separator = path.find_last_of('/');
    if (!storage_root && separator != std::string::npos && separator > 0)
      parent = path.substr(0, separator);
    entries.push_back({parent.empty() ? "Choose storage" : "..", parent, true});
    if (DIR *directory = opendir(path.c_str())) {
      while (const auto *item = readdir(directory)) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || entries.size() >= 1024) continue;
        const std::string child = path + "/" + name;
        struct stat info{};
        if (lstat(child.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode) || (S_ISREG(info.st_mode) && info.st_size > 0 && info.st_size <= 65536))
          entries.push_back({name, child, S_ISDIR(info.st_mode)});
      }
      closedir(directory);
    }
    std::sort(entries.begin() + 1, entries.end(), [](const Entry &a, const Entry &b) {
      return a.directory != b.directory ? a.directory : a.name < b.name;
    });
  }
  int y = 0;
  for (const auto &entry : entries) {
    Row(list, y, entry.directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
        entry.name, entry.directory ? "" : "Private key", [state, overlay, entry] {
          if (entry.directory) {
            ChoosePrivateKey(state, entry.path);
            lv_obj_delete_async(overlay);
            return;
          }
          std::ifstream file(entry.path, std::ios::binary);
          std::string key(65537, '\0');
          file.read(&key[0], key.size());
          key.resize(static_cast<size_t>(file.gcount()));
          std::string error;
          const bool imported = !file.bad() && RecoveryImportNasKey(key, entry.name, &error);
          std::fill(key.begin(), key.end(), '\0');
          if (!imported) {
            Sheet(state->screen, "Invalid network storage setting",
                  error.empty() ? "Could not read the private key." : error);
            return;
          }
          auto config = state->snapshot.config;
          config.key_name = entry.name; config.auth = "key";
          if (SaveConfig(state, config)) {
            lv_obj_delete_async(overlay); Populate(state);
          }
        });
    y += 190;
  }
  auto *close = Button(overlay, "Cancel", [overlay] { lv_obj_delete_async(overlay); });
  lv_obj_set_size(close, lv_obj_get_width(state->screen) - 128, 112);
  lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -90);
}

void ChooseWifiNetwork(NasUi *state) {
  if (state->busy) return;
  if (state->snapshot.mounted) {
    Sheet(state->screen, "Network storage is mounted",
          "Unmount it before changing the auto-connect network.");
    return;
  }

  std::vector<std::string> saved;
  for (const auto &network : RecoveryWifiStatus().networks) {
    if (network.saved && !network.ssid.empty() &&
        std::find(saved.begin(), saved.end(), network.ssid) == saved.end())
      saved.push_back(network.ssid);
  }
  std::stable_sort(saved.begin(), saved.end(), [](const auto &a, const auto &b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });

  auto *overlay = lv_obj_create(state->screen);
  lv_obj_set_user_data(overlay, &kModalMarker);
  Clear(overlay);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  MainBackground(overlay);
  lv_obj_update_layout(overlay);
  const int width = lv_obj_get_width(overlay);
  const int height = lv_obj_get_height(overlay);
  auto *title = Label(overlay, "Choose Wi-Fi network",
                      &lv_font_montserrat_48, kText);
  lv_obj_set_pos(title, 80, 130);
  auto *detail = Label(overlay,
      "Select a saved network or enter its SSID manually.",
      &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(detail, 80, 210);

  auto *list = lv_obj_create(overlay);
  Clear(list);
  lv_obj_set_pos(list, 64, 290);
  lv_obj_set_size(list, width - 128, height - 650);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  int y = 0;
  for (const auto &ssid : saved) {
    const bool selected = state->snapshot.config.wifi_ssid == ssid;
    auto *row = Row(list, y, LV_SYMBOL_WIFI, ssid,
                    selected ? "Selected" : "Saved network",
                    [state, overlay, ssid] {
                      auto config = state->snapshot.config;
                      config.wifi_ssid = ssid;
                      if (SaveConfig(state, config)) {
                        lv_obj_delete_async(overlay);
                        Populate(state);
                      }
                    }, selected ? LV_SYMBOL_OK : LV_SYMBOL_RIGHT);
    if (selected) {
      lv_obj_set_style_border_width(row, 2, 0);
      lv_obj_set_style_border_color(row, kAccent, 0);
      lv_obj_set_style_border_opa(row, LV_OPA_70, 0);
    }
    y += 190;
  }
  if (saved.empty()) {
    auto *empty = lv_obj_create(list);
    Panel(empty, 30, kMainPanel);
    lv_obj_set_pos(empty, 0, 0);
    lv_obj_set_size(empty, width - 128, 180);
    auto *empty_title = Label(empty, "No saved networks",
                              &lv_font_montserrat_32, kText);
    lv_obj_set_pos(empty_title, 32, 34);
    auto *empty_copy = Label(empty,
        "Save a network in Wi-Fi settings, or enter its SSID manually.",
        &lv_font_montserrat_24, kMuted);
    lv_obj_set_pos(empty_copy, 32, 98);
  }

  auto *manual = Button(overlay, "Enter SSID manually", [state, overlay] {
    lv_obj_delete_async(overlay);
    EditField(state, Field::kWifiNetwork);
  }, true);
  lv_obj_set_pos(manual, 64, height - 318);
  lv_obj_set_size(manual, width - 128, 112);
  auto *cancel = Button(overlay, "Cancel", [overlay] {
    lv_obj_delete_async(overlay);
  });
  lv_obj_set_pos(cancel, 64, height - 180);
  lv_obj_set_size(cancel, width - 128, 112);
}

void Populate(NasUi *state) {
  if (state == nullptr || state->scene.list == nullptr) return;
  lv_obj_clean(state->scene.list);
  state->snapshot = RecoveryNasStatus();
  if (!state->snapshot.supported) {
    Row(state->scene.list, 0, LV_SYMBOL_WARNING, "Network storage unavailable",
        "SFTP and SMB support are unavailable on this device.", [] {}, "");
    return;
  }
  auto *protocol = lv_obj_create(state->scene.list);
  Panel(protocol, 32, kMainSheet);
  lv_obj_set_pos(protocol, 0, 0);
  lv_obj_set_size(protocol, 1312, 168);
  lv_obj_set_style_border_width(protocol, 1, 0);
  lv_obj_set_style_border_color(protocol, kMainLine, 0);
  lv_obj_set_style_border_opa(protocol, LV_OPA_30, 0);
  auto *protocol_title = Label(protocol, "Connection protocol",
                               &lv_font_montserrat_32, kText);
  lv_obj_set_pos(protocol_title, 32, 34);
  auto *protocol_copy = Label(protocol, "Choose how AERA reaches your server",
                              &lv_font_montserrat_20, kMuted);
  lv_obj_set_pos(protocol_copy, 32, 94);
  ProtocolButton(state, protocol, 696, "sftp", "SFTP");
  ProtocolButton(state, protocol, 994, "smb", "SMB");
  AnimateEnter(protocol, 10, 9);

  int y = 190;
  y += AddAutoConnectCard(state, y) + 22;
  if (state->snapshot.config.type == "sftp") {
    y += AddAuthenticationCard(state, y) + 22;
  }
  AddField(state, 0, y, Field::kHost);
  AddField(state, 672, y, Field::kUser);
  y += 180;
  AddField(state, 0, y, Field::kPassword);
  if (state->snapshot.config.type == "sftp") {
    AddField(state, 672, y, Field::kPort);
  } else {
    AddCacheCard(state, 672, y);
  }
  y += 180;
  if (state->snapshot.config.type == "smb") {
    AddBrowseFoldersCard(state, y);
    y += 180;
    y += AddAdvancedSmbCard(state, y) + 22;
  } else {
    AddField(state, 0, y, Field::kPath);
    AddCacheCard(state, 672, y);
  }
}

void Refresh(NasUi *state) {
  if (state == nullptr) return;
  state->snapshot = RecoveryNasStatus();
  const auto &status = state->snapshot;
  const std::string busy_title = state->busy ? BusyTitle(state) : std::string();
  const char *headline = !status.supported ? "Unavailable" :
      state->busy ? busy_title.c_str() : status.mounted ?
      (status.selected ? "Mounted and in use" : "Mounted") : "Not mounted";
  i18n::BindLabel(state->scene.status, headline);
  std::string detail = status.status;
  if (detail.empty()) detail = status.mounted ? "/mnt/nas" :
      "Configure an SFTP or SMB connection below.";
  i18n::BindLabel(state->scene.detail, detail.c_str());
  lv_obj_set_style_text_color(state->scene.detail, kMutedStrong, 0);
  SetActivity(state, state->busy);
  lv_obj_set_style_border_color(state->hero,
      status.mounted ? kGreen : state->busy ? kAccent : kMainLine, 0);
  lv_obj_set_style_border_opa(state->hero,
      status.mounted || state->busy ? LV_OPA_60 : LV_OPA_30, 0);
  lv_obj_set_style_text_color(state->status_icon,
      status.mounted ? kGreen : kAccent, 0);

  auto *primary_label = lv_obj_get_child(state->scene.primary, 0);
  i18n::BindLabel(primary_label,
      status.mounted ? (status.selected ? "NAS in use" : "Use NAS")
                     : "Mount & Use");
  if (!status.supported || state->busy || (status.mounted && status.selected))
    lv_obj_add_state(state->scene.primary, LV_STATE_DISABLED);
  else
    lv_obj_remove_state(state->scene.primary, LV_STATE_DISABLED);
  if (!status.mounted || state->busy)
    lv_obj_add_state(state->scene.secondary, LV_STATE_DISABLED);
  else
    lv_obj_remove_state(state->scene.secondary, LV_STATE_DISABLED);
}

void Timer(lv_timer_t *timer) {
  auto *state = static_cast<NasUi *>(lv_timer_get_user_data(timer));
  if (state->busy) ++state->busy_phase;
  Refresh(state);
}

}  // namespace

NasScene BuildNasScene(lv_obj_t *screen, ActionCallback callback,
                       void *context) {
  auto *state = new NasUi;
  state->screen = screen;
  state->callback = callback;
  state->context = context;
  state->snapshot = RecoveryNasStatus();
  lv_obj_add_event_cb(screen, [](lv_event_t *event) {
    auto *state = static_cast<NasUi *>(lv_event_get_user_data(event));
    lv_async_call_cancel(DeferredPopulate, state);
    if (state->timer != nullptr) lv_timer_delete(state->timer);
    if (state->picker) {
      state->picker->owner = nullptr;
      if (state->picker->work) state->picker->work->cancel.store(true);
    }
    delete state;
  }, LV_EVENT_DELETE, state);

  Header(screen, "Network Storage", "SFTP & SMB connections", callback, context);
  const bool landscape = Landscape(screen);
  auto *panel = lv_obj_create(screen);
  Panel(panel, 36, kMainSheet);
  state->hero = panel;
  lv_obj_set_pos(panel, 64, landscape ? 350 : 420);
  lv_obj_set_size(panel, landscape ? 1450 : 1312,
                  landscape ? 520 : 352);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, kMainLine, 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_30, 0);

  auto *status_plate = lv_obj_create(panel);
  Panel(status_plate, 34, IconBackground());
  lv_obj_set_pos(status_plate, 40, 38);
  lv_obj_set_size(status_plate, 124, 124);
  lv_obj_set_style_border_width(status_plate, 1, 0);
  lv_obj_set_style_border_color(
      status_plate,
      RecoveryTintedIconBackgrounds() ? kAccent : kMainLine, 0);
  lv_obj_set_style_border_opa(status_plate, LV_OPA_40, 0);
  state->status_icon = Label(status_plate, LV_SYMBOL_DRIVE,
                             &lv_font_montserrat_48, kAccent);
  lv_obj_center(state->status_icon);

  state->scene.status = Label(panel, "", &lv_font_montserrat_48, kText);
  lv_obj_set_pos(state->scene.status, 198, 34);
  lv_obj_set_width(state->scene.status, 1000);
  lv_label_set_long_mode(state->scene.status, LV_LABEL_LONG_DOT);
  state->scene.detail = Label(panel, "", &lv_font_montserrat_24, kMutedStrong);
  lv_obj_set_pos(state->scene.detail, 198, 108);
  lv_obj_set_width(state->scene.detail, 960);
  lv_label_set_long_mode(state->scene.detail, LV_LABEL_LONG_DOT);
  state->activity = Label(panel, LV_SYMBOL_REFRESH,
                          &lv_font_montserrat_32, kAccent);
  lv_obj_set_pos(state->activity, 1190, 62);
  lv_obj_add_flag(state->activity, LV_OBJ_FLAG_HIDDEN);
  state->scene.primary = Button(panel, "Mount & Use", [state] {
    Dispatch(state, state->snapshot.mounted ? NasOperation::kUse
                                            : NasOperation::kMountAndUse);
  });
  lv_obj_set_pos(state->scene.primary, 40, 210);
  lv_obj_set_size(state->scene.primary, 760, 104);
  lv_obj_set_style_radius(state->scene.primary, 30, 0);
  state->scene.secondary = Button(panel, "Unmount", [state] {
    Dispatch(state, NasOperation::kUnmount);
  });
  lv_obj_set_pos(state->scene.secondary, 824, 210);
  lv_obj_set_size(state->scene.secondary, 414, 104);
  lv_obj_set_style_radius(state->scene.secondary, 30, 0);
  lv_obj_set_style_opa(state->scene.primary, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_set_style_opa(state->scene.secondary, LV_OPA_40, LV_STATE_DISABLED);

  auto *caption = Label(screen, "CONNECTION  /  SAVED ON DEVICE",
                        &lv_font_montserrat_18, kMuted);
  lv_obj_set_style_text_letter_space(caption, 3, 0);
  lv_obj_set_pos(caption, landscape ? 1580 : 80,
                 landscape ? 306 : 826);
  state->scene.list = Scroll(screen, landscape ? 350 : 880,
                             landscape ? 900 : 1964);
  if (landscape) {
    lv_obj_set_x(state->scene.list, 1560);
    lv_obj_set_width(state->scene.list, 1544);
  }
  lv_obj_set_style_pad_bottom(state->scene.list, landscape ? 190 : 242, 0);
  state->scene.state = state;
  Navigation(screen, Action::kSettings, callback, context);
  Populate(state);
  Refresh(state);
  AnimateEnter(panel, 10, 14);
  state->timer = lv_timer_create(Timer, 260, state);
  return state->scene;
}

NasRequest GetNasRequest() { return gRequest; }

void SetNasBusy(const NasScene &scene, const NasRequest &request) {
  auto *state = static_cast<NasUi *>(scene.state);
  if (state == nullptr) return;
  state->busy = true;
  state->running = request.operation;
  state->busy_phase = 0;
  Refresh(state);
}

void CompleteNasOperation(const NasScene &scene, bool success) {
  auto *state = static_cast<NasUi *>(scene.state);
  if (state == nullptr) return;
  state->busy = false;
  Refresh(state);
  Populate(state);
  if (!success) {
    const auto status = RecoveryNasStatus();
    Sheet(state->screen, "Network storage action failed",
          status.error.empty() ?
              "Check Wi-Fi, server settings and the recovery log, then try again."
              : status.error);
  }
}

}  // namespace aeraui
