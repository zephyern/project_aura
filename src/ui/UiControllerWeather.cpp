// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Outdoor temperature on the main screen and its info screen (docs/WEATHER.md).
// The layout follows the weather setting; the card shows the latest result
// only — a value after a good check, otherwise the reason, never an older value.

#include "ui/UiController.h"

#include "ui/UiLocalization.h"
#include "ui/UiOutdoorCard.h"
#include "ui/UiText.h"
#include "ui/UiWeatherLayout.h"
#include "ui/fonts.h"
#include "ui/screens.h"

namespace {

const char *outdoor_message_text(UiOutdoorCard::Message message) {
    switch (message) {
        case UiOutdoorCard::Message::SetUp: return UiText::MsgWeatherSetUp();
        case UiOutdoorCard::Message::NoInternet: return UiText::MsgWeatherNoInternet();
        case UiOutdoorCard::Message::BadKey: return UiText::MsgWeatherBadKey();
        case UiOutdoorCard::Message::Service: return UiText::MsgWeatherServiceError();
        case UiOutdoorCard::Message::None: break;
    }
    return "";
}

}  // namespace

void UiController::update_outdoor_card() {
    const bool layout_on = storage.config().weather_enabled;
    if (layout_on != weather_layout_on_) {
        weather_layout_on_ = layout_on;
        UiWeatherLayout::apply(layout_on, {UiLocalization::bodyFont14(ui_language), UiLocalization::bodyFont18(ui_language)});
        outdoor_card_drawn_ = false;
        if (!layout_on && info_sensor == INFO_OUTDOOR && current_screen_id == SCREEN_ID_PAGE_SENSORS_INFO) {
            // Turned off while its chart is open: back to the main screen.
            pending_screen_id = SCREEN_ID_PAGE_MAIN_PRO;
        }
    }
    if (!layout_on) return;

    const WeatherManager::Snapshot snap = weatherManager.snapshot();
    if (outdoor_card_drawn_ && snap.seq == outdoor_shown_.seq && snap.status == outdoor_shown_.status &&
        temp_units_c == outdoor_units_c_shown_) {
        return;
    }
    outdoor_shown_ = snap;
    outdoor_units_c_shown_ = temp_units_c;
    outdoor_card_drawn_ = true;

    const UiOutdoorCard::State state = UiOutdoorCard::resolve(snap.status);
    char buf[16];
    const char *value = UiText::ValueMissing();
    if (state.show_value && !state.waiting &&
        UiOutdoorCard::formatValue(snap.temp_c, temp_units_c, buf, sizeof(buf))) {
        value = buf;
    }
    safe_label_set_text(objects.label_outdoor_value, value);
    if (objects.label_outdoor_value) {
        lv_obj_set_style_text_font(objects.label_outdoor_value,
                                   UiOutdoorCard::useLargeValueFont(value) ? &ui_font_jet_med_48 : &ui_font_jet_med_28,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    safe_label_set_text_static(objects.label_outdoor_unit, temp_units_c ? UiText::UnitC() : UiText::UnitF());
    safe_label_set_text_static(objects.label_outdoor_message, outdoor_message_text(state.message));
    set_visible(objects.label_outdoor_value, state.show_value);
    set_visible(objects.label_outdoor_unit, state.show_value);
    set_visible(objects.label_outdoor_message, !state.show_value);
    if (info_sensor == INFO_OUTDOOR) {
        safe_label_set_text(objects.label_sensor_value, state.show_value ? value : UiText::ValueMissing());
        safe_label_set_text(objects.label_sensor_info_unit, temp_units_c ? UiText::UnitC() : UiText::UnitF());
    }
}

void UiController::update_pressure_info_24h(const char *delta_text) {
    // While weather is on, the compact pressure card has room for the 3-hour
    // change only; the 24-hour change is shown on the pressure info screen.
    // Visibility is set by set_pressure_info_mode (text view only).
    if (!objects.label_pressure_info_24h || !weather_layout_on_) return;
    char text[48];
    snprintf(text, sizeof(text), "24h: %s %s", delta_text, pressure_display_unit());
    safe_label_set_text(objects.label_pressure_info_24h, text);
}

void UiController::on_card_outdoor_event(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    info_sensor = INFO_OUTDOOR;
    restore_sensor_info_selection();
    pending_screen_id = SCREEN_ID_PAGE_SENSORS_INFO;
}

void UiController::on_card_outdoor_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_card_outdoor_event(e);
}
