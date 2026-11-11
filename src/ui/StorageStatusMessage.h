// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>

#include "modules/StorageHealth.h"

// The alert-bar line for the long-term history storage status
// (docs/LONG_HISTORY.md, "Alert-bar messages"). It sits next to the sensor
// messages of StatusMessages and is rotated with them by UiController; it
// carries no severity, so it never colours the header or wakes the backlight.
namespace StorageStatusMessage {

constexpr size_t kTextCapacity = 96;

struct Message {
    bool shown = false; // false for Off and Ok: nothing to say
    StorageHealth::Kind kind = StorageHealth::Kind::Off;
    char text[kTextCapacity] = {};
};

// Text in the active UI language (UiStrings::setLanguage).
Message build(const StorageHealth::Status &status);

} // namespace StorageStatusMessage
