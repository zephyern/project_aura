// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web/WebWeatherHandlers.h"

#include <ArduinoJson.h>
#include <string.h>

#include "modules/StorageManager.h"
#include "modules/WeatherApi.h"
#include "modules/WeatherManager.h"
#include "web/WebResponseUtils.h"

namespace {

void send_json(WebRequest &server, int status_code, ArduinoJson::JsonDocument &doc) {
    String json;
    serializeJson(doc, json);
    WebResponseUtils::sendNoStoreHeaders(server);
    server.send(status_code, "application/json", json);
}

void send_error(WebRequest &server, int status_code, const char *message) {
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = false;
    root["error"] = message ? message : "Unknown error";
    send_json(server, status_code, doc);
}

bool parse_object(WebRequest &server, ArduinoJson::JsonDocument &body) {
    if (deserializeJson(body, server.arg("plain")) != ArduinoJson::DeserializationError::Ok ||
        !body.is<ArduinoJson::JsonObjectConst>()) {
        send_error(server, 400, "Body must be a JSON object");
        return false;
    }
    return true;
}

// Rejects any key not in `allowed`, naming it and the known ones.
bool only_known_fields(WebRequest &server, ArduinoJson::JsonObjectConst obj, const char *const *allowed,
                       size_t count, const char *field_list) {
    for (ArduinoJson::JsonPairConst kv : obj) {
        bool known = false;
        for (size_t i = 0; i < count; ++i) known = known || strcmp(kv.key().c_str(), allowed[i]) == 0;
        if (!known) {
            String message = String("Unknown field: ") + kv.key().c_str() + " (fields: " + field_list + ")";
            send_error(server, 400, message.c_str());
            return false;
        }
    }
    return true;
}

bool ota_blocked(WebRequest &server, bool ota_busy, const char *ota_busy_json) {
    if (!ota_busy) return false;
    WebResponseUtils::sendNoStoreHeaders(server);
    server.send(503, "application/json", ota_busy_json);
    return true;
}

// What the page shows for a status. The device card uses its own
// translated strings (UiStrings) with the same meanings.
const char *status_text(WeatherApi::Status status) {
    switch (status) {
        case WeatherApi::Status::Ok: return "OK";
        case WeatherApi::Status::Off: return "Off";
        case WeatherApi::Status::NotSetUp: return "Not set up";
        case WeatherApi::Status::Waiting: return "Waiting for the first reading";
        case WeatherApi::Status::NoNetwork: return "No internet";
        case WeatherApi::Status::KeyRejected: return "Bad API key";
        case WeatherApi::Status::ServiceError:
        case WeatherApi::Status::BadJson:
        case WeatherApi::Status::MissingField: return "Weather service error";
    }
    return "Weather service error";
}

void apply_to_manager(WebHandlerContext &context) {
    const Config::StoredConfig &cfg = context.storage->config();
    context.weather->configure(cfg.weather_enabled, cfg.weather_api_key, cfg.weather_location_set,
                               cfg.weather_lat, cfg.weather_lon);
}

void fill_settings(ArduinoJson::JsonObject root, const WebHandlerContext &context) {
    const Config::StoredConfig &cfg = context.storage->config();
    root["enabled"] = cfg.weather_enabled;
    // Only whether a key exists; the key itself never leaves the device.
    root["key_saved"] = cfg.weather_api_key.length() > 0;
    root["location_set"] = cfg.weather_location_set;
    if (cfg.weather_location_set) {
        root["place_label"] = cfg.weather_place_label;
        root["lat"] = cfg.weather_lat;
        root["lon"] = cfg.weather_lon;
    }
    const WeatherManager::Snapshot snap = context.weather->snapshot();
    root["status"] = WeatherApi::statusName(snap.status);
    root["status_text"] = status_text(snap.status);
    if (snap.status == WeatherApi::Status::Ok) {
        root["temp_c"] = snap.temp_c;
        root["dt"] = static_cast<long long>(snap.dt);
    }
}

bool require_ready(WebHandlerContext &context) {
    if (!context.server) return false;
    if (!context.storage || !context.weather) {
        send_error(*context.server, 503, "Weather unavailable");
        return false;
    }
    return true;
}

}  // namespace

namespace WebWeatherHandlers {

void handleSettingsGet(WebHandlerContext &context) {
    if (!require_ready(context)) return;
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = true;
    fill_settings(root, context);
    send_json(*context.server, 200, doc);
}

void handleSettingsPost(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    if (!context.server) return;
    WebRequest &server = *context.server;
    if (ota_blocked(server, ota_busy, ota_busy_json)) return;
    if (!require_ready(context)) return;
    ArduinoJson::JsonDocument body;
    if (!parse_object(server, body)) return;
    ArduinoJson::JsonObjectConst obj = body.as<ArduinoJson::JsonObjectConst>();
    static const char *const kFields[] = {"enabled", "api_key", "location"};
    if (!only_known_fields(server, obj, kFields, 3, "enabled, api_key, location")) return;
    if (!obj["enabled"].is<bool>()) {
        send_error(server, 400, "enabled must be true or false");
        return;
    }
    String new_key;
    const bool replace_key = !obj["api_key"].isNull();
    if (replace_key) {
        const char *key = obj["api_key"].is<const char *>() ? obj["api_key"].as<const char *>() : nullptr;
        const char *problem = key ? WeatherApi::validateKey(key) : "api_key must be text";
        if (problem) {
            send_error(server, 400, problem);
            return;
        }
        new_key = key;
    }
    const bool set_location = !obj["location"].isNull();
    StorageManager::WeatherLocation location;
    if (set_location) {
        ArduinoJson::JsonObjectConst loc = obj["location"].as<ArduinoJson::JsonObjectConst>();
        static const char *const kLocFields[] = {"lat", "lon", "label"};
        if (loc.isNull()) {
            send_error(server, 400, "location must be an object with lat, lon and label");
            return;
        }
        if (!only_known_fields(server, loc, kLocFields, 3, "lat, lon, label")) return;
        if (!loc["lat"].is<double>() || !loc["lon"].is<double>() || !loc["label"].is<const char *>()) {
            send_error(server, 400, "location needs numeric lat and lon and a text label");
            return;
        }
        location.lat = loc["lat"].as<double>();
        location.lon = loc["lon"].as<double>();
        const char *problem = WeatherApi::validateLatLon(location.lat, location.lon);
        if (problem) {
            send_error(server, 400, problem);
            return;
        }
        location.label = loc["label"].as<const char *>();
        if (location.label.length() == 0 || location.label.length() > WeatherApi::kMaxLabelLen) {
            send_error(server, 400, "label must be 1 to 96 characters");
            return;
        }
    }
    if (!context.storage->saveWeatherSettings(obj["enabled"].as<bool>(), replace_key ? &new_key : nullptr,
                                              set_location ? &location : nullptr)) {
        send_error(server, 500, "Failed to save weather settings");
        return;
    }
    apply_to_manager(context);
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = true;
    fill_settings(root, context);
    send_json(server, 200, doc);
}

void handleGeocode(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    if (!context.server) return;
    WebRequest &server = *context.server;
    if (ota_blocked(server, ota_busy, ota_busy_json)) return;
    if (!require_ready(context)) return;
    ArduinoJson::JsonDocument body;
    if (!parse_object(server, body)) return;
    ArduinoJson::JsonObjectConst obj = body.as<ArduinoJson::JsonObjectConst>();
    static const char *const kFields[] = {"kind", "query", "country"};
    if (!only_known_fields(server, obj, kFields, 3, "kind, query, country")) return;
    const char *kind = obj["kind"].is<const char *>() ? obj["kind"].as<const char *>() : "";
    const bool zip = strcmp(kind, "zip") == 0;
    if (!zip && strcmp(kind, "city") != 0) {
        send_error(server, 400, "kind must be city or zip");
        return;
    }
    const char *query = obj["query"].is<const char *>() ? obj["query"].as<const char *>() : nullptr;
    const char *country = obj["country"].is<const char *>() ? obj["country"].as<const char *>() : nullptr;
    const char *problem = WeatherApi::validateQuery(query);
    if (!problem) problem = WeatherApi::validateCountry(country);
    if (problem) {
        send_error(server, 400, problem);
        return;
    }
    if (context.storage->config().weather_api_key.length() == 0) {
        send_error(server, 409, "Save the API key first");
        return;
    }
    WeatherApi::Matches matches;
    const WeatherApi::Status status = context.weather->geocode(zip, query, country, matches);
    if (status != WeatherApi::Status::Ok) {
        send_error(server, 502, status_text(status));
        return;
    }
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = true;
    ArduinoJson::JsonArray list = root["matches"].to<ArduinoJson::JsonArray>();
    for (size_t i = 0; i < matches.count; ++i) {
        ArduinoJson::JsonObject m = list.add<ArduinoJson::JsonObject>();
        m["label"] = matches.items[i].label;
        m["lat"] = matches.items[i].lat;
        m["lon"] = matches.items[i].lon;
    }
    send_json(server, 200, doc);
}

}  // namespace WebWeatherHandlers
