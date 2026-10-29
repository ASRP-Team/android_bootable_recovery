/* Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0 */
#include "ui_components.hpp"
#include "power_transition.hpp"
#include <cctype>
#include <memory>

namespace aeraui {
void BuildRebootScene(lv_obj_t *screen, ActionCallback callback, void *context) {
  using namespace widgets;
  using namespace design;
  Header(screen, "Reboot", "Choose a destination or manage the active boot slot.",
         callback, context);
  const bool landscape = Landscape(screen);

  std::string active = RecoverySlot();
  std::transform(active.begin(), active.end(), active.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::toupper(value));
                 });
  if (active != "A" && active != "B") active = "—";

  auto *slot_card = lv_obj_create(screen);
  Panel(slot_card, 38, kMainSheet);
  lv_obj_set_pos(slot_card, 64, landscape ? 340 : 450);
  lv_obj_set_size(slot_card, 1312, 374);
  lv_obj_set_style_border_width(slot_card, 1, 0);
  lv_obj_set_style_border_color(slot_card, kMainLine, 0);
  lv_obj_set_style_border_opa(slot_card, LV_OPA_30, 0);

  auto *slot_icon = lv_obj_create(slot_card);
  Panel(slot_icon, 25, IconBackground());
  lv_obj_set_pos(slot_icon, 32, 34);
  lv_obj_set_size(slot_icon, 94, 94);
  auto *slot_symbol = Label(slot_icon, LV_SYMBOL_SHUFFLE,
                            &lv_font_montserrat_32, kAccent);
  lv_obj_center(slot_symbol);

  auto *caption = Label(slot_card, "ACTIVE BOOT SLOT",
                        &lv_font_montserrat_24, kMutedStrong);
  lv_obj_set_pos(caption, 158, 36);
  lv_obj_set_style_text_letter_space(caption, 2, 0);
  auto *current = Label(slot_card, i18n::Format("Slot %s", active.c_str()).c_str(),
                        &lv_font_montserrat_48, kText);
  lv_obj_set_pos(current, 158, 78);
  auto *hint = Label(slot_card,
      "Switching slots can brick your device.",
      &lv_font_montserrat_24, kRed);
  lv_obj_set_pos(hint, 34, 154);
  lv_obj_set_width(hint, 1220);

  auto *feedback = Label(slot_card, "", &lv_font_montserrat_24, kRed);
  lv_obj_set_pos(feedback, 34, 332);
  lv_obj_set_width(feedback, 1220);

  for (int index = 0; index < 2; ++index) {
    const std::string slot(1, static_cast<char>('A' + index));
    const bool selected = active == slot;
    const std::string slot_label = i18n::Format("Slot %s", slot.c_str());
    auto *button = Button(slot_card, slot_label.c_str(), [=] {
      if (selected) return;
      const std::string title =
          i18n::Format("Switch to Slot %s?", slot.c_str());
      const std::string detail = i18n::Format(
          "ASRP will mark Slot %s active. This does not reboot the device yet.",
          slot.c_str());
      struct Review {
        bool acknowledged = false;
        lv_obj_t *slider = nullptr;
      };
      auto review = std::make_shared<Review>();
      Sheet(screen, title.c_str(), detail.c_str(), [=] {
        if (!review->acknowledged) return;
        if (RecoverySetActiveSlot(slot)) {
          callback(Action::kOpenReboot, context);
        } else {
          i18n::BindLabel(feedback,
                            "Could not change the active boot slot.");
        }
      }, 1640, false, SheetPresentation::kStandard, "Swipe to change slot",
      [review, detail](lv_obj_t *body) {
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(body, 28, 0);
        auto text = [body](const char *copy, lv_color_t color) {
          auto *label = Label(body, copy, &lv_font_montserrat_32, color);
          lv_obj_set_width(label, LV_PCT(100));
          lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        };
        text("Switching slots can brick your device.", kRed);
        text("The other slot may be empty, incomplete or incompatible. Changing slots does not copy or repair Android or firmware.", kText);
        text("Android, recovery and fastboot may become inaccessible. Unbricking may require service tools and erase your data.", kText);
        text("Only continue if you know the target slot is complete and compatible. Do not switch slots just to experiment.", kMutedStrong);
        text(detail.c_str(), kMutedStrong);
        auto *ack = lv_button_create(body);
        Panel(ack, 24, kMainPanel);
        Interactive(ack, kMainSelected);
        lv_obj_set_size(ack, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(ack, 24, 0);
        lv_obj_set_style_pad_column(ack, 24, 0);
        lv_obj_set_style_min_height(ack, 128, 0);
        lv_obj_set_flex_flow(ack, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ack, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                             LV_FLEX_ALIGN_CENTER);
        auto *check = lv_checkbox_create(ack);
        lv_checkbox_set_text(check, "");
        lv_obj_remove_flag(check, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(check, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_style_size(check, 42, 42, LV_PART_INDICATOR);
        lv_obj_set_style_text_font(check, UiFont(&lv_font_montserrat_32), LV_PART_MAIN);
        lv_obj_set_style_pad_all(check, 20, LV_PART_INDICATOR);
        lv_obj_set_style_radius(check, 12, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(check, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(check, kInset, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(check, 2, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(check, kMutedStrong, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(check, kRed, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(check, lv_color_white(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_image_src(check, LV_SYMBOL_OK, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_image_recolor(check, lv_color_white(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_image_opa(check, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
        auto *label = Label(ack, "I understand the risks and have checked the target slot.",
                            &lv_font_montserrat_28, kText);
        lv_obj_set_width(label, 0);
        lv_obj_set_flex_grow(label, 1);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        OnClick(ack, [review, check] {
          review->acknowledged = !review->acknowledged;
          if (review->acknowledged) lv_obj_add_state(check, LV_STATE_CHECKED);
          else lv_obj_remove_state(check, LV_STATE_CHECKED);
          if (review->slider) {
            if (review->acknowledged) lv_obj_remove_state(review->slider, LV_STATE_DISABLED);
            else lv_obj_add_state(review->slider, LV_STATE_DISABLED);
            lv_obj_set_style_opa(review->slider,
                review->acknowledged ? LV_OPA_COVER : LV_OPA_40, 0);
          }
        });
      }, [review](lv_obj_t *slider, lv_obj_t *) {
        review->slider = slider;
        lv_obj_add_state(slider, LV_STATE_DISABLED);
        lv_obj_set_style_opa(slider, LV_OPA_40, 0);
        lv_obj_set_style_border_color(slider, kRed, 0);
      });
    }, selected);
    lv_obj_set_pos(button, 34 + index * 630, 212);
    lv_obj_set_size(button, 606, 104);
    if (selected) {
      auto *check = Label(button, LV_SYMBOL_OK, &lv_font_montserrat_24,
                          kOnAccent);
      lv_obj_align(check, LV_ALIGN_RIGHT_MID, -34, 0);
    }
  }
  AnimateEnter(slot_card, 20, 10);

  auto *section = Label(screen, "REBOOT DESTINATION",
                        &lv_font_montserrat_24, kMutedStrong);
  lv_obj_set_pos(section, landscape ? 1450 : 80,
                 landscape ? 340 : 888);
  lv_obj_set_style_text_letter_space(section, 2, 0);

  struct Destination { const char *icon, *name, *detail; Action action; };
  const std::array<Destination, 5> destinations{{
    {LV_SYMBOL_HOME, "Android", "Leave recovery and start the system", Action::kRebootSystem},
    {LV_SYMBOL_REFRESH, "Recovery", "Restart ASRP Recovery Project", Action::kRebootRecovery},
    {LV_SYMBOL_SETTINGS, "Bootloader", "Restart in hardware fastboot mode", Action::kRebootBootloader},
    {LV_SYMBOL_USB, "Fastbootd", "Userspace fastboot for logical partitions", Action::kRebootFastbootd},
    {LV_SYMBOL_POWER, "Power off", "Turn off the device", Action::kPowerOff}}};
  auto *list = Scroll(screen, landscape ? 392 : 948,
                      landscape ? 820 : 1730);
  if (landscape) {
    lv_obj_set_x(list, 1436);
    lv_obj_set_width(list, 1668);
  }
  for (size_t i = 0; i < destinations.size(); ++i) {
    const auto d = destinations[i];
    const int column = static_cast<int>(i % 2);
    const int row = static_cast<int>(i / 2);
    auto *card = lv_button_create(list);
    Panel(card, 34, kMainSheet);
    Interactive(card, kMainSelected);
    lv_obj_set_pos(card, column * (landscape ? 842 : 672),
                   row * (landscape ? 372 : 292));
    lv_obj_set_size(card, landscape ? 818 : 640,
                    landscape ? 344 : 260);
    lv_obj_set_style_transform_scale(card, 256, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card,
                                  d.action == Action::kPowerOff ? kRed : kMainLine,
                                  0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    OnClick(card, [=] {
      const bool power_off = d.action == Action::kPowerOff;
      const std::string title = power_off
          ? i18n::Translate("Power off device?") : i18n::Format("Reboot to %s?", i18n::Translate(d.name));
      std::string detail = std::string(i18n::Translate(d.detail)) + ".\n\n";
      if (active != "—" && !power_off)
        detail += i18n::Format("Active slot: %s", active.c_str()) + "  •  ";
      detail += i18n::Translate("The current recovery session will end.");
      Sheet(screen, title, detail, [=] {
        if (d.action == Action::kRebootFastbootd) {
          const uint32_t transition_duration =
              RecoveryFastbootWifiStatus().enabled ? 1080 : 720;
          ModeTransition(screen, true,
                         [=] { callback(d.action, context); },
                         transition_duration);
          return;
        }
        const std::string transition_title = power_off
            ? i18n::Translate("Powering off") : i18n::Format("Rebooting to %s", i18n::Translate(d.name));
        PowerTransition(screen, transition_title,
                        [=] { callback(d.action, context); });
      },
            0, false, SheetPresentation::kCompactGlass,
            power_off ? "Slide to power off" : "Slide to reboot");
    });

    const bool power_off = d.action == Action::kPowerOff;
    auto *plate = lv_obj_create(card);
    Panel(plate, 24,
          RecoveryTintedIconBackgrounds()
              ? (power_off ? kRedSoft : kAccentSoft)
              : kMainPanel);
    lv_obj_set_pos(plate, 30, 30);
    lv_obj_set_size(plate, 92, 92);
    auto *icon = Label(plate, d.icon, &lv_font_montserrat_32,
                       power_off ? kRed : kAccent);
    lv_obj_center(icon);
    auto *name = Label(card, d.name, &lv_font_montserrat_32, kText);
    lv_obj_set_pos(name, 148, 46);
    lv_obj_set_width(name, landscape ? 590 : 420);
    auto *detail = Label(card, d.detail, &lv_font_montserrat_24, kMuted);
    lv_obj_set_pos(detail, 30, 154);
    lv_obj_set_width(detail, landscape ? 738 : 560);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    AnimateEnter(card, 60 + static_cast<uint32_t>(i) * 28, 10);
  }
  Navigation(screen, Action::kSettings, callback, context);
}
}  // namespace aeraui
