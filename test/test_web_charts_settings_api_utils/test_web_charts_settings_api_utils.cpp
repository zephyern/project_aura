// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <ArduinoJson.h>

#include "web/WebChartsSettingsApiUtils.h"

using WebChartsSettingsApiUtils::ParseResult;
using WebChartsSettingsApiUtils::parseRequestBody;

namespace {

constexpr const char *kFieldList = "(fields: band_enabled, month_enabled, year_enabled, lifetime_enabled, multi_enabled)";

// Every field, all set to `value`, plus `extra` (a JSON member or "").
String body(bool value, const char *extra = "") {
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) root[field.key] = value;
    String out;
    serializeJson(doc, out);
    if (extra[0] != '\0') out = String("{") + extra + "," + out.substr(1);
    return out;
}

void assertError(const String &request, const char *expected) {
    const ParseResult result = parseRequestBody(request);
    TEST_ASSERT_FALSE(result.success);
    const String full = String(expected) + " " + kFieldList;
    TEST_ASSERT_EQUAL_STRING(full.c_str(), result.error_message.c_str());
}

} // namespace

void setUp() {}
void tearDown() {}

void test_every_setting_defaults_off() {
    const ChartsSettings::Values defaults;
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) {
        TEST_ASSERT_FALSE_MESSAGE(defaults.*field.member, field.key);
    }
}

void test_all_fields_true_and_false_parse() {
    ParseResult on = parseRequestBody(body(true));
    TEST_ASSERT_TRUE(on.success);
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) TEST_ASSERT_TRUE(on.values.*field.member);
    ParseResult off = parseRequestBody(body(false));
    TEST_ASSERT_TRUE(off.success);
    for (const ChartsSettings::Field &field : ChartsSettings::kFields) TEST_ASSERT_FALSE(off.values.*field.member);
}

void test_unknown_field_is_named() {
    assertError(body(true, "\"bands\":true"), "Unknown field: bands");
}

void test_missing_field_is_named() {
    assertError("{}", "Missing field: band_enabled");
    assertError("{\"band_enabled\":true}", "Missing field: month_enabled");
    assertError("{\"band_enabled\":true,\"month_enabled\":true}", "Missing field: year_enabled");
    assertError("{\"band_enabled\":true,\"month_enabled\":true,\"year_enabled\":true}",
                "Missing field: lifetime_enabled");
    assertError("{\"band_enabled\":true,\"month_enabled\":true,\"year_enabled\":true,\"lifetime_enabled\":true}",
                "Missing field: multi_enabled");
}

void test_non_boolean_is_rejected() {
    assertError("{\"band_enabled\":1}", "band_enabled must be true or false");
}

void test_non_object_body_is_rejected() {
    assertError("[true]", "Body must be a JSON object");
    assertError("not json", "Body must be a JSON object");
}

void test_fill_writes_every_field() {
    ChartsSettings::Values values;
    values.band_enabled = true;
    ArduinoJson::JsonDocument doc;
    WebChartsSettingsApiUtils::fillSettingsJson(doc.to<ArduinoJson::JsonObject>(), values);
    TEST_ASSERT_EQUAL_UINT32(ChartsSettings::kFieldCount, doc.as<ArduinoJson::JsonObjectConst>().size());
    TEST_ASSERT_TRUE(doc["band_enabled"].as<bool>());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_every_setting_defaults_off);
    RUN_TEST(test_all_fields_true_and_false_parse);
    RUN_TEST(test_unknown_field_is_named);
    RUN_TEST(test_missing_field_is_named);
    RUN_TEST(test_non_boolean_is_rejected);
    RUN_TEST(test_non_object_body_is_rejected);
    RUN_TEST(test_fill_writes_every_field);
    return UNITY_END();
}
