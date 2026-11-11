// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>

// The optional chart features (docs/CHARTS_HISTORY.md). Every one is off
// unless turned on, so nothing changes until the owner of the device asks.
// Stored in the config file's "charts" section and set on the web page
// (/api/charts/settings); the key list below is the single list both use.
namespace ChartsSettings {

struct Values {
    bool band_enabled = false;  // faded min/max band under the 1h/3h/24h lines
    bool month_enabled = false; // Month range on the device charts
    bool year_enabled = false;  // Year range on the device charts
};

struct Field {
    const char *key;
    bool Values::*member;
};

constexpr Field kFields[] = {
    {"band_enabled", &Values::band_enabled},
    {"month_enabled", &Values::month_enabled},
    {"year_enabled", &Values::year_enabled},
};
constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

} // namespace ChartsSettings
