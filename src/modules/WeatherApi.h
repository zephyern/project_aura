// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Pure pieces of the optional outdoor-temperature feature (docs/WEATHER.md):
// OpenWeatherMap request URLs, response parsing and result classification.
// No network and no device APIs here, so all of it is host-tested.
namespace WeatherApi {

constexpr const char *kHost = "api.openweathermap.org";
// Poll period, aligned to the 5-minute history slots (Daniel, Nov 10 08:16).
constexpr uint32_t kPollPeriodS = 5 * 60;
// Upper bounds of what the user can type on the web page.
constexpr size_t kMaxKeyLen = 64;
constexpr size_t kMaxQueryLen = 96;
constexpr size_t kMaxCountryLen = 2;  // ISO 3166 alpha-2
constexpr size_t kMaxLabelLen = 96;
constexpr size_t kMaxMatches = 5;     // OWM direct geocoding returns at most 5
constexpr size_t kUrlCap = 384;

// Why a weather check produced no reading. Each one is shown at once on the
// card (no grace period, Daniel Nov 10 08:20) and leaves a gap in the chart.
enum class Status : uint8_t {
    Ok,
    Off,           // the weather setting is off
    NotSetUp,      // on, but no key or no confirmed location
    Waiting,       // on and set up, first check not done yet
    NoNetwork,     // Wi-Fi down, DNS/TLS/connect failure or timeout
    KeyRejected,   // HTTP 401
    ServiceError,  // any other HTTP status
    BadJson,       // body is not JSON
    MissingField,  // JSON without the field we need
};

const char *statusName(Status status);

// HTTP outcome → Status. `transport_ok` is false when no HTTP status arrived.
Status classifyHttp(bool transport_ok, int http_status);

struct Reading {
    float temp_c = 0.0f;
    time_t dt = 0;  // OWM's own measurement time (UTC); logged to measure the refresh rate
};

// Current weather body (data/2.5/weather, units=metric) → Ok + reading,
// BadJson or MissingField (main.temp or dt absent / not numbers).
Status parseCurrent(const char *body, size_t len, Reading &out);

struct Match {
    char label[kMaxLabelLen + 1] = {};  // "Millburn, New Jersey, US"
    double lat = 0.0;
    double lon = 0.0;
};

struct Matches {
    Match items[kMaxMatches];
    size_t count = 0;
};

// geo/1.0/direct body (array) → Ok with 0..5 matches, BadJson or MissingField.
Status parseDirectGeocode(const char *body, size_t len, Matches &out);
// geo/1.0/zip body (object) → Ok with exactly 1 match, BadJson or MissingField.
Status parseZipGeocode(const char *body, size_t len, Matches &out);

// Input checks for the web page. Each returns nullptr when valid, otherwise a
// short message naming what is wrong.
const char *validateKey(const char *key);
const char *validateCountry(const char *country);
const char *validateQuery(const char *query);
const char *validateLatLon(double lat, double lon);

// URL builders. Return false when the URL would not fit `cap`.
// The URLs carry the key: never log them.
bool currentUrl(double lat, double lon, const char *key, char *out, size_t cap);
// q = "<query>,<country>"; query is "City" or "City,State" as typed.
bool directGeocodeUrl(const char *query, const char *country, const char *key, char *out, size_t cap);
bool zipGeocodeUrl(const char *zip, const char *country, const char *key, char *out, size_t cap);

// Percent-encodes everything except RFC 3986 unreserved characters.
bool urlEncode(const char *in, char *out, size_t cap);

// First poll slot strictly after `now`, on a multiple of kPollPeriodS.
time_t nextSlot(time_t now);

}  // namespace WeatherApi
