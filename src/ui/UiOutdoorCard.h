// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <stdio.h>
#include <string.h>

#include "modules/WeatherApi.h"

// What the outdoor card shows for one weather result (docs/WEATHER.md):
// hidden while the setting is off, the value after a good check, otherwise
// the reason at once (never an older value). Pure, so it is host tested.
namespace UiOutdoorCard {

enum class Message : uint8_t {
    None,       // show the value (or "---" while waiting)
    SetUp,      // "Set up on web page"
    NoInternet, // "No internet"
    BadKey,     // "Bad API key"
    Service,    // "Weather service error"
};

struct State {
    bool visible = false;   // the card, and the weather layout, are shown
    bool show_value = false;
    bool waiting = false;   // set up, first check not done: value shows "---"
    Message message = Message::None;
};

inline State resolve(WeatherApi::Status status) {
    using S = WeatherApi::Status;
    State s;
    s.visible = status != S::Off;
    switch (status) {
        case S::Off: break;
        case S::Ok: s.show_value = true; break;
        case S::Waiting: s.show_value = true; s.waiting = true; break;
        case S::NotSetUp: s.message = Message::SetUp; break;
        case S::NoNetwork: s.message = Message::NoInternet; break;
        case S::KeyRejected: s.message = Message::BadKey; break;
        case S::ServiceError:
        case S::BadJson:
        case S::MissingField: s.message = Message::Service; break;
    }
    return s;
}

// The value in the indoor temperature unit, one decimal, as the indoor card.
inline bool formatValue(float temp_c, bool units_c, char *out, size_t size) {
    if (!out || size == 0 || !std::isfinite(temp_c)) return false;
    const float shown = units_c ? temp_c : temp_c * 9.0f / 5.0f + 32.0f;
    const int n = snprintf(out, size, "%.1f", static_cast<double>(shown));
    return n > 0 && static_cast<size_t>(n) < size;
}

// The value label is as wide as the indoor one (120 px), which holds four
// characters of the 48 px monospaced digits ("21.6"); longer values such as
// "-12.5" or "100.4" use the 28 px digits (the offline renderer checks both).
constexpr size_t kLargeValueMaxChars = 4;
inline bool useLargeValueFont(const char *text) {
    return text && strlen(text) <= kLargeValueMaxChars;
}

}  // namespace UiOutdoorCard
