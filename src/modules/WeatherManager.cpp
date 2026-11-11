// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/WeatherManager.h"

#include <stdlib.h>

#include "core/Logger.h"

namespace {

constexpr const char *kTag = "Weather";

bool keySet(const String &key) { return key.length() > 0; }

}  // namespace

void WeatherManager::setHttpGet(HttpGet get) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_ = get;
}

void WeatherManager::configure(bool enabled, const String &api_key, bool location_set, double lat, double lon) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
    api_key_ = api_key;
    location_set_ = location_set;
    lat_ = lat;
    lon_ = lon;
    ++generation_;
    due_now_ = true;
    const uint32_t seq = snapshot_.seq + 1;
    snapshot_ = Snapshot{};
    snapshot_.seq = seq;
    if (!enabled_) {
        snapshot_.status = WeatherApi::Status::Off;
    } else if (!keySet(api_key_) || !location_set_) {
        snapshot_.status = WeatherApi::Status::NotSetUp;
    } else {
        snapshot_.status = WeatherApi::Status::Waiting;
    }
}

bool WeatherManager::due(uint32_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || !keySet(api_key_) || !location_set_ || in_flight_) return false;
    // Signed difference so a millis() wrap does not stall polling.
    if (!due_now_ && static_cast<int32_t>(now_ms - next_due_ms_) < 0) return false;
    in_flight_ = true;
    return true;
}

WeatherApi::Status WeatherManager::fetchCurrent(WeatherApi::Reading &reading) {
    HttpGet get = nullptr;
    char url[WeatherApi::kUrlCap];
    {
        std::lock_guard<std::mutex> lock(mutex_);
        get = get_;
        if (!WeatherApi::currentUrl(lat_, lon_, api_key_.c_str(), url, sizeof(url))) {
            // Cannot happen with a validated key; refuse rather than send a cut URL.
            LOGE(kTag, "weather URL does not fit; check skipped");
            return WeatherApi::Status::ServiceError;
        }
    }
    if (!get) {
        LOGE(kTag, "no HTTP client attached");
        return WeatherApi::Status::NoNetwork;
    }
    char *body = static_cast<char *>(malloc(kCurrentBodyCap));
    if (!body) {
        LOGE(kTag, "no memory for the weather response");
        return WeatherApi::Status::ServiceError;
    }
    int http_status = 0;
    size_t len = 0;
    bool truncated = false;
    const bool transport_ok = get(url, http_status, body, kCurrentBodyCap, len, truncated);
    WeatherApi::Status status = WeatherApi::classifyHttp(transport_ok, http_status);
    if (status == WeatherApi::Status::Ok) {
        status = truncated ? WeatherApi::Status::BadJson : WeatherApi::parseCurrent(body, len, reading);
    }
    if (status != WeatherApi::Status::Ok) {
        LOGW(kTag, "weather check failed: %s (HTTP %d)", WeatherApi::statusName(status), http_status);
    }
    free(body);
    return status;
}

void WeatherManager::check(uint32_t now_ms, time_t wall_now, bool time_trusted, bool wifi_connected) {
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = generation_;
        if (!enabled_ || !keySet(api_key_) || !location_set_) {
            in_flight_ = false;
            return;
        }
    }
    WeatherApi::Reading reading;
    const WeatherApi::Status status = wifi_connected ? fetchCurrent(reading) : WeatherApi::Status::NoNetwork;

    std::lock_guard<std::mutex> lock(mutex_);
    in_flight_ = false;
    due_now_ = false;
    const uint32_t delay_s = time_trusted
                                 ? static_cast<uint32_t>(WeatherApi::nextSlot(wall_now) - wall_now)
                                 : WeatherApi::kPollPeriodS;
    next_due_ms_ = now_ms + delay_s * 1000U;
    if (generation != generation_) return;  // settings changed meanwhile: result is for the old ones
    snapshot_.status = status;
    snapshot_.temp_c = status == WeatherApi::Status::Ok ? reading.temp_c : 0.0f;
    snapshot_.dt = status == WeatherApi::Status::Ok ? reading.dt : 0;
    ++snapshot_.seq;
    if (status == WeatherApi::Status::Ok) {
        // dt is logged on purpose: it shows how often the plan refreshes (docs/WEATHER.md).
        LOGI(kTag, "outdoor %.2f C, dt=%lld", static_cast<double>(reading.temp_c),
             static_cast<long long>(reading.dt));
    }
}

WeatherManager::Snapshot WeatherManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

WeatherApi::Status WeatherManager::geocode(bool zip, const char *query, const char *country,
                                           WeatherApi::Matches &out) {
    out = WeatherApi::Matches{};
    HttpGet get = nullptr;
    char url[WeatherApi::kUrlCap];
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!keySet(api_key_)) return WeatherApi::Status::NotSetUp;
        get = get_;
        const bool built = zip ? WeatherApi::zipGeocodeUrl(query, country, api_key_.c_str(), url, sizeof(url))
                               : WeatherApi::directGeocodeUrl(query, country, api_key_.c_str(), url, sizeof(url));
        if (!built) return WeatherApi::Status::ServiceError;
    }
    if (!get) return WeatherApi::Status::NoNetwork;
    char *body = static_cast<char *>(malloc(kGeocodeBodyCap));
    if (!body) {
        LOGE(kTag, "no memory for the geocoding response");
        return WeatherApi::Status::ServiceError;
    }
    int http_status = 0;
    size_t len = 0;
    bool truncated = false;
    const bool transport_ok = get(url, http_status, body, kGeocodeBodyCap, len, truncated);
    WeatherApi::Status status = WeatherApi::classifyHttp(transport_ok, http_status);
    // geo/1.0/zip answers 404 for an unknown code: that is "no match", not a fault.
    if (zip && transport_ok && http_status == 404) {
        status = WeatherApi::Status::Ok;
    } else if (status == WeatherApi::Status::Ok) {
        if (truncated) {
            status = WeatherApi::Status::BadJson;
        } else {
            status = zip ? WeatherApi::parseZipGeocode(body, len, out) : WeatherApi::parseDirectGeocode(body, len, out);
        }
    }
    if (status != WeatherApi::Status::Ok) {
        LOGW(kTag, "geocoding failed: %s (HTTP %d)", WeatherApi::statusName(status), http_status);
    }
    free(body);
    return status;
}
