/* SPDX-License-Identifier: Apache-2.0 */
#include "scene.hpp"
#include "ui_components.hpp"
#include "phone_keyboard.hpp"
#include <map>
#include <memory>

namespace aeraui {
namespace {
using namespace design;
using namespace widgets;
std::string selected_group = "Network";
std::map<std::string, int> scroll_positions;
bool guard_next_entry = false;

void GuardEntry(lv_obj_t *screen) {
  struct Guard {
    lv_obj_t *shield;
    uint32_t started;
    lv_timer_t *timer = nullptr;
    ~Guard() { if (timer) lv_timer_delete(timer); }
  };
  // The new page is visible immediately; only leftover taps are swallowed.
  auto *shield = lv_obj_create(screen);
  Clear(shield);
  lv_obj_set_size(shield, LV_PCT(100), LV_PCT(100));
  lv_obj_set_user_data(shield, &kModalMarker);
  lv_obj_add_flag(shield, LV_OBJ_FLAG_CLICKABLE);
  auto *guard = new Guard{shield, lv_tick_get()};
  lv_obj_add_event_cb(shield, [](lv_event_t *event) {
    delete static_cast<Guard *>(lv_event_get_user_data(event));
  }, LV_EVENT_DELETE, guard);
  guard->timer = lv_timer_create([](lv_timer_t *timer) {
    auto *guard = static_cast<Guard *>(lv_timer_get_user_data(timer));
    if (lv_tick_get() - guard->started < 500) return;
    for (auto *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
      if (lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) return;
    lv_obj_delete(guard->shield);
  }, 25, guard);
}
std::string Value(const kernel::Setting &setting, const std::string &raw) {
  if (raw.empty()) return i18n::Translate("Kernel default");
  if (setting.name.find("frequency") != std::string::npos) {
    const auto n = strtoull(raw.c_str(), nullptr, 10);
    return std::to_string(n / (setting.name.find("kHz") != std::string::npos ? 1000 : 1000000)) + " MHz";
  }
  if (setting.id == "net.ecn") {
    if (raw == "0") return i18n::Translate("Disabled");
    if (raw == "1") return i18n::Translate("Enabled");
    if (raw == "2") return i18n::Translate("Accept incoming only");
  }
  if (setting.id == "net.mtu_probe") {
    if (raw == "0") return i18n::Translate("Disabled");
    if (raw == "1") return i18n::Translate("On connection problems");
    if (raw == "2") return i18n::Translate("Always");
  }
  if (setting.id == "net.idle") return i18n::Translate(raw == "0" ? "Off" : "On");
  if (setting.id == "net.fastopen") {
    const char *names[]{"Disabled", "Client", "Server", "Client and server"};
    const int n = atoi(raw.c_str());
    if (n >= 0 && n < 4) return i18n::Translate(names[n]);
  }
  return raw;
}
void Editor(lv_obj_t *screen, kernel::Setting setting, Handler refresh) {
  auto *overlay = lv_obj_create(screen);
  Clear(overlay);
  lv_obj_set_user_data(overlay, &kModalMarker);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, kMainCanvas, 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  const bool landscape = Landscape(screen);
  const int width = lv_obj_get_width(screen), height = lv_obj_get_height(screen);
  const int content_width = landscape ? width / 2 - 96 : width - 128;
  auto *title = Label(overlay, setting.name.c_str(), &lv_font_montserrat_40, kText);
  lv_obj_set_pos(title, 64, 70); lv_obj_set_width(title, content_width);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  auto *device = Label(overlay, setting.device.c_str(), &lv_font_montserrat_24, kMutedStrong);
  lv_obj_set_pos(device, 64, 160); lv_obj_set_width(device, content_width);
  lv_label_set_long_mode(device, LV_LABEL_LONG_DOT);
  auto chosen = std::make_shared<std::string>(setting.current);
  auto remember = std::make_shared<bool>(true);
  const bool numeric = setting.choices.empty();
  const int controls_y = numeric ? 430 : height - 420;
  lv_obj_t *input = nullptr;
  lv_obj_t *keyboard = nullptr;
  if (numeric) {
    input = TextArea(overlay);
    Panel(input, 24, kMainSheet);
    lv_obj_set_style_text_font(input, UiFont(&lv_font_montserrat_40), 0);
    lv_obj_set_style_text_color(input, kText, 0);
    lv_obj_set_style_pad_all(input, 28, 0);
    lv_obj_set_pos(input, 64, 230); lv_obj_set_size(input, content_width, 124);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 12);
    lv_textarea_set_accepted_chars(input, "0123456789");
    lv_textarea_set_text(input, setting.current.c_str());
    auto *range = Label(overlay, (std::to_string(setting.minimum) + " - " +
        std::to_string(setting.maximum)).c_str(), &lv_font_montserrat_24, kMutedStrong);
    lv_obj_set_pos(range, 64, 370);
    keyboard = lv_keyboard_create(overlay);
    phone_keyboard::Apply(keyboard);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(keyboard, input);
    const int keyboard_height = landscape ? height - 360 : std::min(1200, height - 900);
    lv_obj_set_size(keyboard, landscape ? width / 2 - 96 : width - 128, keyboard_height);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, landscape ? width / 2 + 32 : 64,
                    landscape ? 280 : height - keyboard_height - 64);
  } else {
    auto *choices = lv_obj_create(overlay);
    Clear(choices); lv_obj_add_flag(choices, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(choices, 64, 230);
    lv_obj_set_size(choices, width - 128, controls_y - 260);
    lv_obj_set_flex_flow(choices, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(choices, 14, 0);
    for (const auto &value : setting.choices) {
      auto *row = Button(choices, Value(setting, value).c_str(), {}, value == setting.current);
      lv_obj_set_size(row, LV_PCT(100), 104);
      OnClick(row, [chosen, value, choices, row] {
        *chosen = value;
        for (uint32_t i = 0; i < lv_obj_get_child_count(choices); ++i) {
          auto *child = lv_obj_get_child(choices, i);
          const bool selected = child == row;
          lv_obj_set_style_bg_color(child, selected ? kAccent : kMainPanel, 0);
          lv_obj_set_style_bg_color(child, selected ? kAccentPressed : kMainSelected, LV_STATE_PRESSED);
          lv_obj_set_style_text_color(lv_obj_get_child(child, 0), selected ? kOnAccent : kText, 0);
        }
      });
    }
  }
  auto *restore = Button(overlay, "Restore at startup", {});
  lv_obj_set_pos(restore, 64, controls_y);
  lv_obj_set_size(restore, content_width, 112);
  auto *restore_label = lv_obj_get_child(restore, 0);
  FitLabelToLines(restore_label, content_width - 210, 1,
      {&lv_font_montserrat_32, &lv_font_montserrat_28, &lv_font_montserrat_24});
  lv_obj_align(restore_label, LV_ALIGN_LEFT_MID, 28, 0);
  auto *toggle = lv_switch_create(restore); StyleSwitch(toggle, true);
  lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -28, 0);
  // Let the row own touch input; the switch is a visual status indicator.
  OnClick(restore, [remember, toggle] {
    *remember = !*remember;
    if (*remember) lv_obj_add_state(toggle, LV_STATE_CHECKED);
    else lv_obj_remove_state(toggle, LV_STATE_CHECKED);
  });
  auto *feedback = Label(overlay, "", &lv_font_montserrat_24, kRed);
  lv_obj_set_pos(feedback, 64, controls_y + 130);
  lv_obj_set_width(feedback, content_width);
  lv_label_set_long_mode(feedback, LV_LABEL_LONG_WRAP);
  const int button_width = (content_width - 32) / 3;
  auto action = [&](const char *name, int column, Handler handler, bool primary = false) {
    auto *button = Button(overlay, name, std::move(handler), primary);
    lv_obj_set_pos(button, 64 + column * (button_width + 16), controls_y + 230);
    lv_obj_set_size(button, button_width, 112);
    FitButtonLabel(button);
  };
  action("Cancel", 0, [overlay] { lv_obj_delete_async(overlay); });
  auto apply = [=](bool reset) {
    std::string error;
    const std::string value = reset ? "" : numeric ? lv_textarea_get_text(input) : *chosen;
    if (!RecoverySetDeveloperSetting(setting.id, value, *remember, &error)) {
      lv_label_set_text(feedback, error.c_str()); return;
    }
    lv_obj_delete_async(overlay); refresh();
  };
  action("Kernel default", 1, [apply] { apply(true); });
  action("Apply", 2, [apply] { apply(false); }, true);
  if (keyboard) {
    auto *done = new Handler([apply] { apply(false); });
    lv_obj_add_event_cb(keyboard, [](lv_event_t *event) {
      auto *done = static_cast<Handler *>(lv_event_get_user_data(event));
      if (lv_event_get_code(event) == LV_EVENT_DELETE) delete done;
      else if (lv_event_get_code(event) == LV_EVENT_READY) (*done)();
    }, LV_EVENT_ALL, done);
  }
}
}
void ArmDeveloperEntryGuard() { guard_next_entry = true; }
void BuildDeveloperScene(lv_obj_t *screen, ActionCallback callback, void *context) {
  const bool guard_entry = guard_next_entry;
  guard_next_entry = false;
  Header(screen, "Developer settings", "", callback, context);
  const auto settings = RecoveryDeveloperSettings();
  const int top = Landscape(screen) ? 330 : 410;
  const int width = lv_obj_get_width(screen) - 128;
  auto *tabs = lv_obj_create(screen); Clear(tabs);
  lv_obj_set_pos(tabs, 64, top); lv_obj_set_size(tabs, width, 104);
  lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(tabs, 12, 0);
  for (const char *name : {"Network", "Storage", "CPU", "GPU", "Memory", "Saved"}) {
    auto *tab = Button(tabs, name, [=] {
      selected_group = name; callback(Action::kDeveloperSettings, context);
    }, selected_group == name);
    lv_obj_set_width(tab, 0); lv_obj_set_flex_grow(tab, 1); lv_obj_set_height(tab, 104);
    FitButtonLabel(tab);
  }
  auto *list = Scroll(screen, top + 124,
      lv_obj_get_height(screen) - top - 124 - NavigationHeight(screen) - 28);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 18, 0);
  lv_obj_set_style_pad_bottom(list, 30, 0);
  auto *back = Button(list, LV_SYMBOL_LEFT "  About AERA", [=] { callback(Action::kBack, context); });
  lv_obj_set_size(back, LV_PCT(100), 104);
  auto *note = Label(list, "Experimental. Changes affect this recovery session, not Android. Hold Volume Down during startup to skip saved tuning.",
                     &lv_font_montserrat_28, kAmber);
  lv_obj_set_width(note, LV_PCT(100)); lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  auto refresh = [=] {
    scroll_positions[selected_group] = lv_obj_get_scroll_y(list);
    callback(Action::kDeveloperSettings, context);
  };
  if (selected_group == "Storage") {
    const bool enabled = RecoveryPreference(Preference::kPreserveSplash);
    const bool supported = RecoverySplashPreservationSupported();
    auto *row = lv_button_create(list);
    Panel(row, 28, kMainSheet); Interactive(row, kMainSelected);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 28, 0);
    lv_obj_set_style_pad_column(row, 24, 0);
    auto *text = lv_obj_create(row); Clear(text);
    lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(text, 0); lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 12, 0);
    auto *title = Label(text, "Keep splash", &lv_font_montserrat_32, kText);
    lv_obj_set_width(title, LV_PCT(100)); lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    auto *detail = Label(text, supported ?
        "Keep the current boot splash during ZIP installs. Manual image flashing can override this." :
        "Unavailable", &lv_font_montserrat_24, kMutedStrong);
    lv_obj_set_width(detail, LV_PCT(100)); lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    auto *toggle = lv_switch_create(row); StyleSwitch(toggle, enabled);
    if (!supported && !enabled) lv_obj_add_state(row, LV_STATE_DISABLED);
    else OnClick(row, [=] {
      const bool previous = RecoveryPreference(Preference::kPreserveSplash);
      if (!RecoverySetPreference(Preference::kPreserveSplash, !previous)) return;
      if (!RecoveryAutoSavePreferences()) {
        RecoverySetPreference(Preference::kPreserveSplash, previous);
        Sheet(screen, "Developer settings", "Could not save preferences");
        return;
      }
      if (!previous) lv_obj_add_state(toggle, LV_STATE_CHECKED);
      else lv_obj_remove_state(toggle, LV_STATE_CHECKED);
    });
  }
  bool any = false;
  for (const auto &setting : settings) {
    if (selected_group == "Saved" ? setting.saved.empty() : setting.group != selected_group) continue;
    any = true;
    auto *row = lv_button_create(list); Panel(row, 28, kMainSheet); Interactive(row, kMainSelected);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(row, 28, 0); lv_obj_set_style_pad_row(row, 12, 0);
    auto label = [&](const std::string &text, const lv_font_t *font, lv_color_t color) {
      auto *item = Label(row, text.c_str(), font, color);
      lv_obj_set_width(item, LV_PCT(100)); lv_label_set_long_mode(item, LV_LABEL_LONG_WRAP);
    };
    label(setting.device, &lv_font_montserrat_24, kMutedStrong);
    label(setting.name, &lv_font_montserrat_32, kText);
    label(std::string(i18n::Translate("Active")) + ": " + Value(setting, setting.current), &lv_font_montserrat_28, kAccent);
    label(std::string(i18n::Translate("Kernel default")) + ": " + Value(setting, setting.baseline) +
        " / " + i18n::Translate("Saved") + ": " + Value(setting, setting.saved), &lv_font_montserrat_24, kMutedStrong);
    if (!setting.status.empty()) label(i18n::Translate(setting.status.c_str()), &lv_font_montserrat_24, kAmber);
    if (setting.path.empty()) OnClick(row, [=] {
      Sheet(screen, "Forget saved override?", setting.id, [=] {
        std::string error;
        if (RecoverySetDeveloperSetting(setting.id, "", true, &error)) refresh();
        else Sheet(screen, "Developer settings", error);
      });
    });
    else if (setting.writable) OnClick(row, [=] { Editor(screen, setting, refresh); });
    else { lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE); label(i18n::Translate("Read only"), &lv_font_montserrat_24, kMuted); }
  }
  if (!any) {
    auto *empty = Label(list, "No supported kernel settings are available in this category.", &lv_font_montserrat_32, kMutedStrong);
    lv_obj_set_width(empty, LV_PCT(100)); lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
  }
  auto *rescan = Button(list, LV_SYMBOL_REFRESH, refresh); lv_obj_set_size(rescan, LV_PCT(100), 112);
  auto *export_settings = Button(list, "Export kernel settings", [=] {
    std::string path, error;
    if (RecoveryExportDeveloperSettings(&path, &error)) Sheet(screen, "Export kernel settings", path);
    else Sheet(screen, "Developer settings", error);
  });
  lv_obj_set_size(export_settings, LV_PCT(100), 112);
  auto *reset = Button(list, "Reset developer settings", [=] {
    Sheet(screen, "Reset developer settings",
        std::string(i18n::Translate("Restore the initial values from this recovery boot and remove saved overrides?")) +
        "\n" + i18n::Translate("Keep splash") + ": " + i18n::Translate("Off"), [=] {
      std::string error;
      if (!RecoveryResetDeveloperSettings(&error)) Sheet(screen, "Developer settings", error);
      else { scroll_positions.clear(); refresh(); }
    });
  });
  lv_obj_set_size(reset, LV_PCT(100), 112);
  lv_obj_update_layout(list);
  lv_obj_scroll_to_y(list, scroll_positions[selected_group], LV_ANIM_OFF);
  Navigation(screen, Action::kSettings, callback, context);
  if (guard_entry) GuardEntry(screen);
}
}  // namespace aeraui
