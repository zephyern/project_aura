// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "modules/WeatherApi.h"

using WeatherApi::Status;

namespace {

Status parseCur(const char *body, WeatherApi::Reading &r) {
    return WeatherApi::parseCurrent(body, strlen(body), r);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_current_weather_fixture_parses_temp_and_dt() {
    // Trimmed real-shape body of data/2.5/weather?units=metric.
    const char *body =
        "{\"coord\":{\"lon\":-74.3045,\"lat\":40.7245},\"weather\":[{\"id\":800,\"main\":\"Clear\"}],"
        "\"main\":{\"temp\":7.42,\"feels_like\":5.1,\"pressure\":1021,\"humidity\":61},"
        "\"dt\":1794380400,\"name\":\"Millburn\",\"cod\":200}";
    WeatherApi::Reading r;
    TEST_ASSERT_EQUAL(Status::Ok, parseCur(body, r));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.42f, r.temp_c);
    TEST_ASSERT_EQUAL_INT64(1794380400LL, static_cast<long long>(r.dt));
}

void test_negative_and_integer_temperatures_parse() {
    WeatherApi::Reading r;
    TEST_ASSERT_EQUAL(Status::Ok, parseCur("{\"main\":{\"temp\":-12},\"dt\":5}", r));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -12.0f, r.temp_c);
}

void test_current_weather_errors_are_typed() {
    WeatherApi::Reading r;
    TEST_ASSERT_EQUAL(Status::BadJson, parseCur("<html>502</html>", r));
    TEST_ASSERT_EQUAL(Status::BadJson, parseCur("", r));
    TEST_ASSERT_EQUAL(Status::MissingField, parseCur("{\"dt\":5}", r));
    TEST_ASSERT_EQUAL(Status::MissingField, parseCur("{\"main\":{\"temp\":3.0}}", r));
    TEST_ASSERT_EQUAL(Status::MissingField, parseCur("{\"main\":{\"temp\":\"3\"},\"dt\":5}", r));
    TEST_ASSERT_EQUAL(Status::MissingField, parseCur("[1,2]", r));
}

void test_http_classification() {
    TEST_ASSERT_EQUAL(Status::NoNetwork, WeatherApi::classifyHttp(false, 0));
    TEST_ASSERT_EQUAL(Status::Ok, WeatherApi::classifyHttp(true, 200));
    TEST_ASSERT_EQUAL(Status::KeyRejected, WeatherApi::classifyHttp(true, 401));
    TEST_ASSERT_EQUAL(Status::ServiceError, WeatherApi::classifyHttp(true, 429));
    TEST_ASSERT_EQUAL(Status::ServiceError, WeatherApi::classifyHttp(true, 500));
    TEST_ASSERT_EQUAL(Status::ServiceError, WeatherApi::classifyHttp(true, 404));
}

void test_direct_geocode_lists_up_to_five_matches_with_labels() {
    const char *body =
        "[{\"name\":\"Millburn\",\"lat\":40.7245,\"lon\":-74.3045,\"country\":\"US\",\"state\":\"New Jersey\"},"
        "{\"name\":\"Millburn\",\"lat\":55.1,\"lon\":-3.2,\"country\":\"GB\"}]";
    WeatherApi::Matches m;
    TEST_ASSERT_EQUAL(Status::Ok, WeatherApi::parseDirectGeocode(body, strlen(body), m));
    TEST_ASSERT_EQUAL_UINT32(2, m.count);
    TEST_ASSERT_EQUAL_STRING("Millburn, New Jersey, US", m.items[0].label);
    TEST_ASSERT_EQUAL_STRING("Millburn, GB", m.items[1].label);
    TEST_ASSERT_TRUE(fabs((m.items[0].lat) - (40.7245)) < 1e-4);  // ArduinoJson may store a float
    TEST_ASSERT_TRUE(fabs((m.items[0].lon) - (-74.3045)) < 1e-4);  // ArduinoJson may store a float
}

void test_direct_geocode_empty_array_is_ok_with_no_matches() {
    WeatherApi::Matches m;
    TEST_ASSERT_EQUAL(Status::Ok, WeatherApi::parseDirectGeocode("[]", 2, m));
    TEST_ASSERT_EQUAL_UINT32(0, m.count);
}

void test_direct_geocode_caps_at_five() {
    const char *one = "{\"name\":\"A\",\"lat\":1,\"lon\":2}";
    char body[512] = "[";
    for (int i = 0; i < 7; ++i) {
        strcat(body, one);
        strcat(body, i < 6 ? "," : "]");
    }
    WeatherApi::Matches m;
    TEST_ASSERT_EQUAL(Status::Ok, WeatherApi::parseDirectGeocode(body, strlen(body), m));
    TEST_ASSERT_EQUAL_UINT32(WeatherApi::kMaxMatches, m.count);
}

void test_geocode_errors_are_typed() {
    WeatherApi::Matches m;
    TEST_ASSERT_EQUAL(Status::BadJson, WeatherApi::parseDirectGeocode("nope", 4, m));
    const char *no_lat = "[{\"name\":\"A\",\"lon\":2}]";
    TEST_ASSERT_EQUAL(Status::MissingField, WeatherApi::parseDirectGeocode(no_lat, strlen(no_lat), m));
    const char *obj = "{\"cod\":401,\"message\":\"Invalid API key\"}";
    TEST_ASSERT_EQUAL(Status::MissingField, WeatherApi::parseDirectGeocode(obj, strlen(obj), m));
    TEST_ASSERT_EQUAL(Status::MissingField, WeatherApi::parseZipGeocode(obj, strlen(obj), m));
}

void test_zip_geocode_gives_one_match() {
    const char *body = "{\"zip\":\"07041\",\"name\":\"Millburn\",\"lat\":40.7245,\"lon\":-74.3045,\"country\":\"US\"}";
    WeatherApi::Matches m;
    TEST_ASSERT_EQUAL(Status::Ok, WeatherApi::parseZipGeocode(body, strlen(body), m));
    TEST_ASSERT_EQUAL_UINT32(1, m.count);
    TEST_ASSERT_EQUAL_STRING("Millburn, US", m.items[0].label);
}

void test_long_label_is_cut_not_refused() {
    char body[400];
    char name[200];
    memset(name, 'x', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    snprintf(body, sizeof(body), "[{\"name\":\"%s\",\"lat\":1,\"lon\":2,\"country\":\"US\"}]", name);
    WeatherApi::Matches m;
    TEST_ASSERT_EQUAL(Status::Ok, WeatherApi::parseDirectGeocode(body, strlen(body), m));
    TEST_ASSERT_EQUAL_UINT32(WeatherApi::kMaxLabelLen, strlen(m.items[0].label));
}

void test_validation_messages() {
    TEST_ASSERT_NULL(WeatherApi::validateKey("0123456789abcdef0123456789abcdef"));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateKey(""));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateKey("abc def"));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateKey("abc&appid=x"));
    char long_key[WeatherApi::kMaxKeyLen + 2];
    memset(long_key, 'a', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';
    TEST_ASSERT_NOT_NULL(WeatherApi::validateKey(long_key));
    TEST_ASSERT_NULL(WeatherApi::validateCountry("US"));
    TEST_ASSERT_NULL(WeatherApi::validateCountry("gb"));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateCountry(""));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateCountry("USA"));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateCountry("U1"));
    TEST_ASSERT_NULL(WeatherApi::validateQuery("Millburn,NJ"));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateQuery(""));
    TEST_ASSERT_NULL(WeatherApi::validateLatLon(40.7, -74.3));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateLatLon(91.0, 0.0));
    TEST_ASSERT_NOT_NULL(WeatherApi::validateLatLon(0.0, -181.0));
}

void test_urls_are_encoded_and_carry_metric_units() {
    char url[WeatherApi::kUrlCap];
    TEST_ASSERT_TRUE(WeatherApi::currentUrl(40.7245, -74.3045, "KEY", url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://api.openweathermap.org/data/2.5/weather?lat=40.7245&lon=-74.3045&units=metric&appid=KEY", url);
    TEST_ASSERT_TRUE(WeatherApi::directGeocodeUrl("São Paulo, SP", "BR", "KEY", url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://api.openweathermap.org/geo/1.0/direct?q=S%C3%A3o%20Paulo%2C%20SP%2CBR&limit=5&appid=KEY", url);
    TEST_ASSERT_TRUE(WeatherApi::zipGeocodeUrl("07041", "US", "KEY", url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING("https://api.openweathermap.org/geo/1.0/zip?zip=07041%2CUS&appid=KEY", url);
    // A query cannot inject extra parameters.
    TEST_ASSERT_TRUE(WeatherApi::directGeocodeUrl("a&appid=evil", "US", "KEY", url, sizeof(url)));
    TEST_ASSERT_NULL(strstr(url, "&appid=evil"));
}

void test_url_builders_refuse_overflow() {
    char small[32];
    TEST_ASSERT_FALSE(WeatherApi::currentUrl(1.0, 2.0, "KEY", small, sizeof(small)));
    TEST_ASSERT_FALSE(WeatherApi::urlEncode("ab cd", small, 5));
}

void test_next_slot_is_strictly_after_now_on_five_minute_marks() {
    TEST_ASSERT_EQUAL_INT64(300, static_cast<long long>(WeatherApi::nextSlot(0)));
    TEST_ASSERT_EQUAL_INT64(600, static_cast<long long>(WeatherApi::nextSlot(300)));
    TEST_ASSERT_EQUAL_INT64(600, static_cast<long long>(WeatherApi::nextSlot(599)));
    TEST_ASSERT_EQUAL_INT64(1794380700LL, static_cast<long long>(WeatherApi::nextSlot(1794380401)));
}

void test_status_names_are_stable() {
    TEST_ASSERT_EQUAL_STRING("key_rejected", WeatherApi::statusName(Status::KeyRejected));
    TEST_ASSERT_EQUAL_STRING("no_network", WeatherApi::statusName(Status::NoNetwork));
    TEST_ASSERT_EQUAL_STRING("not_set_up", WeatherApi::statusName(Status::NotSetUp));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_current_weather_fixture_parses_temp_and_dt);
    RUN_TEST(test_negative_and_integer_temperatures_parse);
    RUN_TEST(test_current_weather_errors_are_typed);
    RUN_TEST(test_http_classification);
    RUN_TEST(test_direct_geocode_lists_up_to_five_matches_with_labels);
    RUN_TEST(test_direct_geocode_empty_array_is_ok_with_no_matches);
    RUN_TEST(test_direct_geocode_caps_at_five);
    RUN_TEST(test_geocode_errors_are_typed);
    RUN_TEST(test_zip_geocode_gives_one_match);
    RUN_TEST(test_long_label_is_cut_not_refused);
    RUN_TEST(test_validation_messages);
    RUN_TEST(test_urls_are_encoded_and_carry_metric_units);
    RUN_TEST(test_url_builders_refuse_overflow);
    RUN_TEST(test_next_slot_is_strictly_after_now_on_five_minute_marks);
    RUN_TEST(test_status_names_are_stable);
    return UNITY_END();
}
