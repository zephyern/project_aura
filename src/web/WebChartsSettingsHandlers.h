// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "web/WebContext.h"

// GET/POST /api/charts/settings: the optional chart features
// (docs/CHARTS_HISTORY.md). A saved change applies to the device charts at
// their next refresh; no restart.
namespace WebChartsSettingsHandlers {

void handleGet(WebHandlerContext &context);
void handlePost(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json);

} // namespace WebChartsSettingsHandlers
