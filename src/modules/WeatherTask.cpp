// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/WeatherTask.h"

#include <Arduino.h>
#include <atomic>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#include "core/Logger.h"

namespace WeatherTask {

namespace {

constexpr const char *kTag = "Weather";
// TLS handshakes need several KB of stack.
constexpr uint32_t kStackBytes = 10 * 1024;
constexpr UBaseType_t kPriority = 1;
constexpr int kTimeoutMs = 10000;

WeatherManager *g_manager = nullptr;
TaskHandle_t g_task = nullptr;
std::atomic<bool> g_time_trusted{false};
std::atomic<bool> g_wifi_connected{false};

void taskMain(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        g_manager->check(millis(), time(nullptr), g_time_trusted.load(), g_wifi_connected.load());
    }
}

}  // namespace

bool httpGet(const char *url, int &http_status, char *body, size_t cap, size_t &len, bool &truncated) {
    http_status = 0;
    len = 0;
    truncated = false;
    if (!body || cap < 2) return false;
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kTimeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
        http_status = esp_http_client_get_status_code(client);
        ok = true;
        for (;;) {
            const int got = esp_http_client_read(client, body + len, static_cast<int>(cap - 1 - len));
            if (got < 0) {
                ok = false;  // connection broke mid-body
                break;
            }
            if (got == 0) break;
            len += static_cast<size_t>(got);
            if (len >= cap - 1) {
                char extra = 0;
                truncated = esp_http_client_read(client, &extra, 1) > 0;
                break;
            }
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    body[len] = '\0';
    return ok;
}

bool begin(WeatherManager &manager) {
    if (g_task) return true;
    g_manager = &manager;
    manager.setHttpGet(httpGet);
    if (xTaskCreate(taskMain, "weather", kStackBytes, nullptr, kPriority, &g_task) != pdPASS) {
        g_task = nullptr;
        LOGE(kTag, "weather task could not be created; outdoor temperature disabled");
        return false;
    }
    return true;
}

void kick(bool time_trusted, bool wifi_connected) {
    if (!g_task) return;
    g_time_trusted.store(time_trusted);
    g_wifi_connected.store(wifi_connected);
    xTaskNotifyGive(g_task);
}

}  // namespace WeatherTask
