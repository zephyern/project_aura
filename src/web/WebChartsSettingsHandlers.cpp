// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web/WebChartsSettingsHandlers.h"

#include <ArduinoJson.h>

#include "modules/StorageManager.h"
#include "web/WebChartsSettingsApiUtils.h"
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
    root["error"] = message;
    send_json(server, status_code, doc);
}

void send_settings(WebRequest &server, const ChartsSettings::Values &values) {
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = true;
    WebChartsSettingsApiUtils::fillSettingsJson(root, values);
    send_json(server, 200, doc);
}

} // namespace

namespace WebChartsSettingsHandlers {

void handleGet(WebHandlerContext &context) {
    if (!context.server) return;
    if (!context.storage) {
        send_error(*context.server, 503, "Storage unavailable");
        return;
    }
    send_settings(*context.server, context.storage->config().charts);
}

void handlePost(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    WebRequest *server = context.server;
    if (!server) return;
    if (ota_busy) {
        WebResponseUtils::sendNoStoreHeaders(*server);
        server->send(503, "application/json", ota_busy_json);
        return;
    }
    if (!context.storage) {
        send_error(*server, 503, "Storage unavailable");
        return;
    }
    const WebChartsSettingsApiUtils::ParseResult parsed =
        WebChartsSettingsApiUtils::parseRequestBody(server->arg("plain"));
    if (!parsed.success) {
        send_error(*server, 400, parsed.error_message.c_str());
        return;
    }
    if (!context.storage->saveChartsSettings(parsed.values)) {
        send_error(*server, 500, "Failed to save chart settings");
        return;
    }
    send_settings(*server, context.storage->config().charts);
}

} // namespace WebChartsSettingsHandlers
