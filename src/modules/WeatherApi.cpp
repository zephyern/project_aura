// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/WeatherApi.h"

#include <ArduinoJson.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

namespace WeatherApi {

namespace {

bool isUnreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '_' || c == '~';
}

bool appendLabelPart(char *label, size_t cap, const char *part) {
    if (!part || part[0] == '\0') return true;
    const size_t used = strlen(label);
    const int n = snprintf(label + used, cap - used, "%s%s", used ? ", " : "", part);
    return n >= 0 && static_cast<size_t>(n) < cap - used;
}

// Fills one match from an OWM geocoding object. Missing name/lat/lon is
// MissingField; state and country are optional parts of the label.
Status fillMatch(ArduinoJson::JsonObjectConst obj, Match &out) {
    if (!obj["name"].is<const char *>() || !obj["lat"].is<double>() || !obj["lon"].is<double>()) {
        return Status::MissingField;
    }
    out = Match{};
    out.lat = obj["lat"].as<double>();
    out.lon = obj["lon"].as<double>();
    char label[kMaxLabelLen + 1] = {};
    // A label longer than the field is cut, never refused: it is only shown.
    appendLabelPart(label, sizeof(label), obj["name"].as<const char *>());
    appendLabelPart(label, sizeof(label), obj["state"].as<const char *>());
    appendLabelPart(label, sizeof(label), obj["country"].as<const char *>());
    memcpy(out.label, label, sizeof(out.label));
    return Status::Ok;
}

}  // namespace

const char *statusName(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::Off: return "off";
        case Status::NotSetUp: return "not_set_up";
        case Status::Waiting: return "waiting";
        case Status::NoNetwork: return "no_network";
        case Status::KeyRejected: return "key_rejected";
        case Status::ServiceError: return "service_error";
        case Status::BadJson: return "bad_json";
        case Status::MissingField: return "missing_field";
    }
    return "unknown";
}

Status classifyHttp(bool transport_ok, int http_status) {
    if (!transport_ok) return Status::NoNetwork;
    if (http_status == 200) return Status::Ok;
    if (http_status == 401) return Status::KeyRejected;
    return Status::ServiceError;
}

Status parseCurrent(const char *body, size_t len, Reading &out) {
    ArduinoJson::JsonDocument doc;
    if (!body || deserializeJson(doc, body, len) != ArduinoJson::DeserializationError::Ok) {
        return Status::BadJson;
    }
    ArduinoJson::JsonVariantConst temp = doc["main"]["temp"];
    ArduinoJson::JsonVariantConst dt = doc["dt"];
    if (!temp.is<float>() || !dt.is<long long>()) return Status::MissingField;
    const float t = temp.as<float>();
    if (!std::isfinite(t)) return Status::MissingField;
    out.temp_c = t;
    out.dt = static_cast<time_t>(dt.as<long long>());
    return Status::Ok;
}

Status parseDirectGeocode(const char *body, size_t len, Matches &out) {
    out = Matches{};
    ArduinoJson::JsonDocument doc;
    if (!body || deserializeJson(doc, body, len) != ArduinoJson::DeserializationError::Ok) {
        return Status::BadJson;
    }
    if (!doc.is<ArduinoJson::JsonArrayConst>()) return Status::MissingField;
    for (ArduinoJson::JsonVariantConst item : doc.as<ArduinoJson::JsonArrayConst>()) {
        if (out.count == kMaxMatches) break;
        if (!item.is<ArduinoJson::JsonObjectConst>()) return Status::MissingField;
        const Status s = fillMatch(item.as<ArduinoJson::JsonObjectConst>(), out.items[out.count]);
        if (s != Status::Ok) return s;
        ++out.count;
    }
    return Status::Ok;
}

Status parseZipGeocode(const char *body, size_t len, Matches &out) {
    out = Matches{};
    ArduinoJson::JsonDocument doc;
    if (!body || deserializeJson(doc, body, len) != ArduinoJson::DeserializationError::Ok) {
        return Status::BadJson;
    }
    if (!doc.is<ArduinoJson::JsonObjectConst>()) return Status::MissingField;
    const Status s = fillMatch(doc.as<ArduinoJson::JsonObjectConst>(), out.items[0]);
    if (s != Status::Ok) return s;
    out.count = 1;
    return Status::Ok;
}

const char *validateKey(const char *key) {
    const size_t n = key ? strlen(key) : 0;
    if (n == 0) return "API key is empty";
    if (n > kMaxKeyLen) return "API key is too long";
    for (size_t i = 0; i < n; ++i) {
        const char c = key[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!ok) return "API key may only contain letters and digits";
    }
    return nullptr;
}

const char *validateCountry(const char *country) {
    if (!country || strlen(country) != kMaxCountryLen) return "Country must be a 2-letter code (for example US)";
    for (size_t i = 0; i < kMaxCountryLen; ++i) {
        const char c = country[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            return "Country must be a 2-letter code (for example US)";
        }
    }
    return nullptr;
}

const char *validateQuery(const char *query) {
    const size_t n = query ? strlen(query) : 0;
    if (n == 0) return "City or ZIP is empty";
    if (n > kMaxQueryLen) return "City or ZIP is too long";
    return nullptr;
}

const char *validateLatLon(double lat, double lon) {
    if (!std::isfinite(lat) || lat < -90.0 || lat > 90.0) return "Latitude must be between -90 and 90";
    if (!std::isfinite(lon) || lon < -180.0 || lon > 180.0) return "Longitude must be between -180 and 180";
    return nullptr;
}

bool urlEncode(const char *in, char *out, size_t cap) {
    static const char kHex[] = "0123456789ABCDEF";
    if (!in || !out || cap == 0) return false;
    size_t o = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(in); *p; ++p) {
        if (isUnreserved(static_cast<char>(*p))) {
            if (o + 1 >= cap) return false;
            out[o++] = static_cast<char>(*p);
        } else {
            if (o + 3 >= cap) return false;
            out[o++] = '%';
            out[o++] = kHex[*p >> 4];
            out[o++] = kHex[*p & 0x0F];
        }
    }
    out[o] = '\0';
    return true;
}

bool currentUrl(double lat, double lon, const char *key, char *out, size_t cap) {
    const int n = snprintf(out, cap,
                           "https://%s/data/2.5/weather?lat=%.4f&lon=%.4f&units=metric&appid=%s",
                           kHost, lat, lon, key ? key : "");
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool directGeocodeUrl(const char *query, const char *country, const char *key, char *out, size_t cap) {
    char q[kMaxQueryLen + kMaxCountryLen + 2] = {};
    const int qn = snprintf(q, sizeof(q), "%s,%s", query ? query : "", country ? country : "");
    if (qn < 0 || static_cast<size_t>(qn) >= sizeof(q)) return false;
    char enc[3 * sizeof(q)] = {};
    if (!urlEncode(q, enc, sizeof(enc))) return false;
    const int n = snprintf(out, cap, "https://%s/geo/1.0/direct?q=%s&limit=%u&appid=%s", kHost, enc,
                           static_cast<unsigned>(kMaxMatches), key ? key : "");
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool zipGeocodeUrl(const char *zip, const char *country, const char *key, char *out, size_t cap) {
    char z[kMaxQueryLen + kMaxCountryLen + 2] = {};
    const int zn = snprintf(z, sizeof(z), "%s,%s", zip ? zip : "", country ? country : "");
    if (zn < 0 || static_cast<size_t>(zn) >= sizeof(z)) return false;
    char enc[3 * sizeof(z)] = {};
    if (!urlEncode(z, enc, sizeof(enc))) return false;
    const int n = snprintf(out, cap, "https://%s/geo/1.0/zip?zip=%s&appid=%s", kHost, enc, key ? key : "");
    return n > 0 && static_cast<size_t>(n) < cap;
}

time_t nextSlot(time_t now) {
    const time_t period = static_cast<time_t>(kPollPeriodS);
    return (now / period + 1) * period;
}

}  // namespace WeatherApi
