// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>
#include <mutex>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "modules/WeatherApi.h"

// Optional outdoor temperature (docs/WEATHER.md). Holds the settings copy,
// decides when a check is due, runs a check through an injected HTTP GET and
// keeps the latest result. The device runs check() on its own task
// (WeatherTask) so the UI never waits on the network; host tests call it
// directly with a fake GET.
class WeatherManager {
public:
    // Fetches `url`. Returns false when no HTTP status arrived (no Wi-Fi,
    // DNS, TLS, timeout). `truncated` is set when the body did not fit `cap`.
    using HttpGet = bool (*)(const char *url, int &http_status, char *body, size_t cap, size_t &len,
                             bool &truncated);

    struct Snapshot {
        WeatherApi::Status status = WeatherApi::Status::Off;
        float temp_c = 0.0f;  // valid only when status == Ok
        time_t dt = 0;        // OWM measurement time of that reading
        uint32_t seq = 0;     // bumps on every new result, for "changed" checks
    };

    // Body buffers. A direct-geocoding answer carries every place's
    // local_names, so it gets the larger one.
    static constexpr size_t kCurrentBodyCap = 4096;
    static constexpr size_t kGeocodeBodyCap = 48 * 1024;

    void setHttpGet(HttpGet get);

    // New settings (boot or web save). Drops the old reading and makes the
    // next check due at once.
    void configure(bool enabled, const String &api_key, bool location_set, double lat, double lon);

    // True when the worker should run check() now. Never true while one runs.
    bool due(uint32_t now_ms);
    // One check (blocking). Sets the result and schedules the next one:
    // on the next 5-minute mark when the clock is trusted, else 5 minutes on.
    void check(uint32_t now_ms, time_t wall_now, bool time_trusted, bool wifi_connected);

    Snapshot snapshot() const;

    // Web setup lookup with the saved key. KeyRejected/ServiceError/... as for
    // a check; NotSetUp when no key is saved. `zip` picks geo/1.0/zip.
    WeatherApi::Status geocode(bool zip, const char *query, const char *country, WeatherApi::Matches &out);

    // Test seams.
    uint32_t nextDueMsForTest() const { return next_due_ms_; }

private:
    WeatherApi::Status fetchCurrent(WeatherApi::Reading &reading);

    mutable std::mutex mutex_;
    HttpGet get_ = nullptr;
    bool enabled_ = false;
    String api_key_;
    bool location_set_ = false;
    double lat_ = 0.0;
    double lon_ = 0.0;
    bool in_flight_ = false;
    uint32_t generation_ = 0;  // bumps on configure(); a check for older settings is dropped
    bool due_now_ = true;
    uint32_t next_due_ms_ = 0;
    Snapshot snapshot_{};
};
