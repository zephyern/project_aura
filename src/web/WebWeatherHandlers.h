// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "web/WebContext.h"

// Web setup for the optional outdoor temperature (docs/WEATHER.md):
//   GET  /api/weather/settings  on/off, whether a key is saved (never the key),
//                               the confirmed place and the latest status.
//   POST /api/weather/settings  {"enabled": bool, "api_key"?: str,
//                                "location"?: {"lat","lon","label"}}
//   POST /api/weather/geocode   {"kind": "city"|"zip", "query": str, "country": str}
//                               → up to five matches for the user to confirm.
namespace WebWeatherHandlers {

void handleSettingsGet(WebHandlerContext &context);
void handleSettingsPost(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json);
void handleGeocode(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json);

}  // namespace WebWeatherHandlers
