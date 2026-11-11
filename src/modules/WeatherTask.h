// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "modules/WeatherManager.h"

// Device side of the optional outdoor temperature (docs/WEATHER.md): the
// HTTPS GET (esp_http_client + the certificate bundle) and the task that runs
// WeatherManager::check() off the main loop. Not built for host tests.
namespace WeatherTask {

// Fetches `url` over HTTPS. See WeatherManager::HttpGet.
bool httpGet(const char *url, int &http_status, char *body, size_t cap, size_t &len, bool &truncated);

// Creates the task and attaches httpGet to `manager`. False when the task
// could not be created (logged); weather checks then never run.
bool begin(WeatherManager &manager);

// Asks the task to run one check now with the main loop's view of the clock
// and Wi-Fi. Call only after WeatherManager::due() returned true.
void kick(bool time_trusted, bool wifi_connected);

}  // namespace WeatherTask
