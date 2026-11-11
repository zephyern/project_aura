// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config/ChartsSettings.h"

// /api/charts/settings (docs/CHARTS_HISTORY.md): the JSON shape of the
// optional chart features. Strict: every field of ChartsSettings::kFields is
// required, each must be true or false, and an unknown field is an error.
namespace WebChartsSettingsApiUtils {

struct ParseResult {
    bool success = false;
    String error_message; // names the field and lists the accepted ones
    ChartsSettings::Values values{};
};

ParseResult parseRequestBody(const String &body);
void fillSettingsJson(ArduinoJson::JsonObject root, const ChartsSettings::Values &values);

} // namespace WebChartsSettingsApiUtils
