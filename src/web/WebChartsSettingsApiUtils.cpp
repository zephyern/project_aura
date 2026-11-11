// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web/WebChartsSettingsApiUtils.h"

#include <string.h>

namespace WebChartsSettingsApiUtils {

namespace {

String fieldList() {
    String list;
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) {
        if (list.length() != 0) list += ", ";
        list += field.key;
    }
    return list;
}

bool isKnown(const char *key) {
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) {
        if (strcmp(key, field.key) == 0) return true;
    }
    return false;
}

ParseResult failure(const String &message) {
    ParseResult result;
    result.error_message = message + " (fields: " + fieldList() + ")";
    return result;
}

} // namespace

ParseResult parseRequestBody(const String &body) {
    ArduinoJson::JsonDocument doc;
    if (deserializeJson(doc, body) != ArduinoJson::DeserializationError::Ok ||
        !doc.is<ArduinoJson::JsonObjectConst>()) {
        return failure("Body must be a JSON object");
    }
    ArduinoJson::JsonObjectConst obj = doc.as<ArduinoJson::JsonObjectConst>();
    for (ArduinoJson::JsonPairConst kv : obj) {
        if (!isKnown(kv.key().c_str())) return failure(String("Unknown field: ") + kv.key().c_str());
    }
    ParseResult result;
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) {
        ArduinoJson::JsonVariantConst value = obj[field.key];
        if (value.isNull()) return failure(String("Missing field: ") + field.key);
        if (!value.is<bool>()) return failure(String(field.key) + " must be true or false");
        result.values.*field.member = value.as<bool>();
    }
    result.success = true;
    return result;
}

void fillSettingsJson(ArduinoJson::JsonObject root, const ChartsSettings::Values &values) {
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) {
        root[field.key] = values.*field.member;
    }
}

} // namespace WebChartsSettingsApiUtils
