// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "ArduinoMock.h"
#include "modules/StorageManager.h"
#include "modules/WeatherManager.h"
#include "web/WebContext.h"
#include "web/WebWeatherHandlers.h"

namespace {

// Same shape as the fake in test_web_ota_handlers.
class FakeRequest final : public WebRequest {
public:
    std::map<String, String> args;
    std::vector<std::pair<String, String>> headers;
    WebUpload current_upload;
    size_t received = 0;
    bool connected = true;
    bool rejected = false;
    bool stopped = false;
    uint32_t upload_deadline_ms = 0;
    size_t pending_body = 0;
    size_t drained = 0;
    size_t drain_limit = 0;
    uint32_t drain_timeout = 0;
    int response_status = 0;
    String response_type;
    String response;

    bool hasArg(const char *name) const override { return args.count(name) != 0; }
    String arg(const char *name) const override {
        const auto found = args.find(name);
        return found == args.end() ? "" : found->second;
    }
    String uri() const override { return "/api/ota"; }
    void sendHeader(const char *name, const String &value, bool = false) override {
        headers.emplace_back(name, value);
    }
    void send(int status, const char *type, const String &content) override {
        response_status = status;
        response_type = type;
        response = content;
    }
    void send(int status, const char *type, const char *content) override {
        send(status, type, String(content));
    }
    bool clientConnected() const override { return connected; }
    void setUploadDeadlineMs(uint32_t timeout_ms) override { upload_deadline_ms = timeout_ms; }
    void clearUploadDeadline() override { upload_deadline_ms = 0; }
    void rejectUpload() override { rejected = true; }
    bool uploadRejected() const override { return rejected; }
    size_t pendingRequestBodyBytes() const override { return pending_body; }
    size_t drainPendingRequestBody(size_t max_bytes, uint32_t max_time_ms) override {
        drain_limit = max_bytes;
        drain_timeout = max_time_ms;
        const size_t amount = std::min(pending_body, max_bytes);
        pending_body -= amount;
        drained += amount;
        return amount;
    }
    void stopClient() override { stopped = true; connected = false; }
    bool beginStreamResponse(int, const char *, size_t, bool = false) override { return false; }
    int32_t writeStreamChunk(const uint8_t *, size_t, int &error) override {
        error = 0;
        return -1;
    }
    bool waitUntilWritable(uint16_t, int &error) override { error = 0; return false; }
    void endStreamResponse() override {}
    WebUpload upload() override { return current_upload; }

    ArduinoJson::JsonDocument json() const {
        ArduinoJson::JsonDocument document;
        const auto error = deserializeJson(document, response);
        TEST_ASSERT_FALSE_MESSAGE(error, response.c_str());
        return document;
    }

    String header(const char *name) const {
        for (const auto &entry : headers) {
            if (entry.first == name) {
                return entry.second;
            }
        }
        return "";
    }
};

const char *kGood = "{\"main\":{\"temp\":7.5},\"dt\":1794380400}";
int g_status = 200;
const char *g_body = kGood;
int g_calls = 0;

bool fakeGet(const char *, int &http_status, char *body, size_t cap, size_t &len, bool &truncated) {
    ++g_calls;
    http_status = g_status;
    len = std::min(strlen(g_body), cap - 1);
    memcpy(body, g_body, len);
    body[len] = '\0';
    truncated = false;
    return true;
}

struct Harness {
    FakeRequest request;
    StorageManager storage;
    WeatherManager weather;
    WebHandlerContext context;
    Harness() {
        storage.begin();
        weather.setHttpGet(fakeGet);
        weather.configure(false, String(""), false, 0, 0);
        context.server = &request;
        context.storage = &storage;
        context.weather = &weather;
        g_status = 200;
        g_body = kGood;
        g_calls = 0;
    }
    void post(const char *body, void (*fn)(WebHandlerContext &, bool, const char *)) {
        request.args["plain"] = body;
        fn(context, false, "{\"busy\":true}");
    }
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_get_starts_off_with_nothing_saved() {
    Harness h;
    WebWeatherHandlers::handleSettingsGet(h.context);
    TEST_ASSERT_EQUAL(200, h.request.response_status);
    ArduinoJson::JsonDocument doc = h.request.json();
    TEST_ASSERT_FALSE(doc["enabled"].as<bool>());
    TEST_ASSERT_FALSE(doc["key_saved"].as<bool>());
    TEST_ASSERT_FALSE(doc["location_set"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("off", doc["status"].as<const char *>());
}

void test_save_key_and_place_never_echoes_the_key() {
    Harness h;
    h.post("{\"enabled\":true,\"api_key\":\"SecretKey99\",\"location\":{\"lat\":40.7245,\"lon\":-74.3045,"
           "\"label\":\"Millburn, New Jersey, US\"}}",
           WebWeatherHandlers::handleSettingsPost);
    TEST_ASSERT_EQUAL_MESSAGE(200, h.request.response_status, h.request.response.c_str());
    TEST_ASSERT_NULL(strstr(h.request.response.c_str(), "SecretKey99"));
    ArduinoJson::JsonDocument doc = h.request.json();
    TEST_ASSERT_TRUE(doc["key_saved"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("waiting", doc["status"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("Millburn, New Jersey, US", doc["place_label"].as<const char *>());
    WebWeatherHandlers::handleSettingsGet(h.context);
    TEST_ASSERT_NULL(strstr(h.request.response.c_str(), "SecretKey99"));
    TEST_ASSERT_EQUAL_STRING("SecretKey99", h.storage.config().weather_api_key.c_str());
}

void test_settings_post_is_strict() {
    struct Case { const char *body; const char *error_part; };
    const Case cases[] = {
        {"{}", "enabled"},
        {"{\"enabled\":\"yes\"}", "enabled"},
        {"{\"enabled\":true,\"colour\":1}", "Unknown field: colour"},
        {"{\"enabled\":true,\"api_key\":\"has space\"}", ""},
        {"{\"enabled\":true,\"location\":{\"lat\":91,\"lon\":0,\"label\":\"x\"}}", ""},
        {"{\"enabled\":true,\"location\":{\"lat\":1,\"lon\":2}}", "location"},
        {"{\"enabled\":true,\"location\":{\"lat\":1,\"lon\":2,\"label\":\"x\",\"zip\":1}}", "Unknown field: zip"},
        {"[1]", "JSON object"},
    };
    for (const Case &c : cases) {
        Harness h;
        h.post(c.body, WebWeatherHandlers::handleSettingsPost);
        TEST_ASSERT_EQUAL_MESSAGE(400, h.request.response_status, c.body);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(h.request.response.c_str(), c.error_part), h.request.response.c_str());
        TEST_ASSERT_FALSE(h.storage.config().weather_enabled);
    }
}

void test_geocode_needs_a_key_then_lists_matches() {
    Harness h;
    h.post("{\"kind\":\"city\",\"query\":\"Millburn,NJ\",\"country\":\"US\"}", WebWeatherHandlers::handleGeocode);
    TEST_ASSERT_EQUAL(409, h.request.response_status);
    TEST_ASSERT_EQUAL(0, g_calls);

    h.post("{\"enabled\":false,\"api_key\":\"abc123\"}", WebWeatherHandlers::handleSettingsPost);
    TEST_ASSERT_EQUAL(200, h.request.response_status);
    g_body = "[{\"name\":\"Millburn\",\"lat\":40.72,\"lon\":-74.3,\"country\":\"US\",\"state\":\"New Jersey\"}]";
    h.post("{\"kind\":\"city\",\"query\":\"Millburn,NJ\",\"country\":\"US\"}", WebWeatherHandlers::handleGeocode);
    TEST_ASSERT_EQUAL_MESSAGE(200, h.request.response_status, h.request.response.c_str());
    ArduinoJson::JsonDocument doc = h.request.json();
    TEST_ASSERT_EQUAL(1, doc["matches"].size());
    TEST_ASSERT_EQUAL_STRING("Millburn, New Jersey, US", doc["matches"][0]["label"].as<const char *>());

    g_status = 401;
    h.post("{\"kind\":\"zip\",\"query\":\"07041\",\"country\":\"US\"}", WebWeatherHandlers::handleGeocode);
    TEST_ASSERT_EQUAL(502, h.request.response_status);
    TEST_ASSERT_NOT_NULL(strstr(h.request.response.c_str(), "Bad API key"));

    h.post("{\"kind\":\"town\",\"query\":\"x\",\"country\":\"US\"}", WebWeatherHandlers::handleGeocode);
    TEST_ASSERT_EQUAL(400, h.request.response_status);
    h.post("{\"kind\":\"zip\",\"query\":\"07041\"}", WebWeatherHandlers::handleGeocode);
    TEST_ASSERT_EQUAL(400, h.request.response_status);
}

void test_ota_busy_blocks_writes() {
    Harness h;
    h.request.args["plain"] = "{\"enabled\":true}";
    WebWeatherHandlers::handleSettingsPost(h.context, true, "{\"busy\":true}");
    TEST_ASSERT_EQUAL(503, h.request.response_status);
    TEST_ASSERT_FALSE(h.storage.config().weather_enabled);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_get_starts_off_with_nothing_saved);
    RUN_TEST(test_save_key_and_place_never_echoes_the_key);
    RUN_TEST(test_settings_post_is_strict);
    RUN_TEST(test_geocode_needs_a_key_then_lists_matches);
    RUN_TEST(test_ota_busy_blocks_writes);
    return UNITY_END();
}
