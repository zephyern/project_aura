// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "core/Logger.h"
#include "modules/WeatherManager.h"

using WeatherApi::Status;

namespace {

struct Fake {
    bool transport_ok = true;
    int http_status = 200;
    const char *body = "";
    bool truncated = false;
    int calls = 0;
    char last_url[WeatherApi::kUrlCap] = {};
} fake;

bool fakeGet(const char *url, int &http_status, char *body, size_t cap, size_t &len, bool &truncated) {
    ++fake.calls;
    snprintf(fake.last_url, sizeof(fake.last_url), "%s", url);
    http_status = fake.http_status;
    len = strlen(fake.body);
    if (len >= cap) len = cap - 1;
    memcpy(body, fake.body, len);
    body[len] = '\0';
    truncated = fake.truncated;
    return fake.transport_ok;
}

const char *kGood = "{\"main\":{\"temp\":7.5},\"dt\":1794380400}";

void ready(WeatherManager &m) {
    fake = Fake{};
    fake.body = kGood;
    m.setHttpGet(fakeGet);
    m.configure(true, String("abc123"), true, 40.7245, -74.3045);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_off_and_not_set_up_states_never_poll() {
    fake = Fake{};
    WeatherManager m;
    m.setHttpGet(fakeGet);
    TEST_ASSERT_EQUAL(Status::Off, m.snapshot().status);
    TEST_ASSERT_FALSE(m.due(0));
    m.configure(false, String("abc"), true, 1, 2);
    TEST_ASSERT_EQUAL(Status::Off, m.snapshot().status);
    TEST_ASSERT_FALSE(m.due(0));
    m.configure(true, String(""), true, 1, 2);
    TEST_ASSERT_EQUAL(Status::NotSetUp, m.snapshot().status);
    TEST_ASSERT_FALSE(m.due(0));
    m.configure(true, String("abc"), false, 0, 0);
    TEST_ASSERT_EQUAL(Status::NotSetUp, m.snapshot().status);
    TEST_ASSERT_FALSE(m.due(0));
    TEST_ASSERT_EQUAL(0, fake.calls);
}

void test_set_up_waits_then_reads_and_logs_dt() {
    WeatherManager m;
    ready(m);
    TEST_ASSERT_EQUAL(Status::Waiting, m.snapshot().status);
    TEST_ASSERT_TRUE(m.due(1000));
    TEST_ASSERT_FALSE(m.due(1000));  // in flight
    m.check(1000, 1794380401, true, true);
    const WeatherManager::Snapshot s = m.snapshot();
    TEST_ASSERT_EQUAL(Status::Ok, s.status);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, s.temp_c);
    TEST_ASSERT_EQUAL_INT64(1794380400LL, static_cast<long long>(s.dt));
    TEST_ASSERT_NOT_NULL(strstr(fake.last_url, "lat=40.7245&lon=-74.3045&units=metric"));
}

void test_next_check_lands_on_the_next_five_minute_mark() {
    WeatherManager m;
    ready(m);
    TEST_ASSERT_TRUE(m.due(1000));
    m.check(1000, 1794380401, true, true);  // next mark 1794380700 → 299 s on
    TEST_ASSERT_EQUAL_UINT32(1000 + 299000, m.nextDueMsForTest());
    TEST_ASSERT_FALSE(m.due(1000 + 298999));
    TEST_ASSERT_TRUE(m.due(1000 + 299000));
}

void test_untrusted_clock_polls_every_five_minutes() {
    WeatherManager m;
    ready(m);
    TEST_ASSERT_TRUE(m.due(5));
    m.check(5, 12345, false, true);
    TEST_ASSERT_EQUAL_UINT32(5 + 300000, m.nextDueMsForTest());
}

void test_due_survives_millis_wrap() {
    WeatherManager m;
    ready(m);
    TEST_ASSERT_TRUE(m.due(0xFFFFFF00u));
    m.check(0xFFFFFF00u, 0, false, true);
    TEST_ASSERT_FALSE(m.due(0xFFFFFFF0u));
    TEST_ASSERT_TRUE(m.due(0xFFFFFF00u + 300000u));
}

void test_each_failure_shows_at_once_and_drops_the_old_value() {
    struct Case { bool transport; int http; const char *body; bool truncated; bool wifi; Status want; };
    const Case cases[] = {
        {true, 200, kGood, false, false, Status::NoNetwork},
        {false, 0, "", false, true, Status::NoNetwork},
        {true, 401, "{\"cod\":401}", false, true, Status::KeyRejected},
        {true, 500, "oops", false, true, Status::ServiceError},
        {true, 429, "{}", false, true, Status::ServiceError},
        {true, 200, "<html>", false, true, Status::BadJson},
        {true, 200, kGood, true, true, Status::BadJson},
        {true, 200, "{\"dt\":1}", false, true, Status::MissingField},
    };
    for (const Case &c : cases) {
        WeatherManager m;
    ready(m);
        TEST_ASSERT_TRUE(m.due(0));
        m.check(0, 0, false, true);
        TEST_ASSERT_EQUAL(Status::Ok, m.snapshot().status);
        fake.transport_ok = c.transport;
        fake.http_status = c.http;
        fake.body = c.body;
        fake.truncated = c.truncated;
        TEST_ASSERT_TRUE(m.due(300000));
        m.check(300000, 0, false, c.wifi);
        const WeatherManager::Snapshot s = m.snapshot();
        TEST_ASSERT_EQUAL(c.want, s.status);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, s.temp_c);  // never the old reading
        TEST_ASSERT_EQUAL_INT64(0, static_cast<long long>(s.dt));
    }
}

void test_no_wifi_makes_no_request() {
    WeatherManager m;
    ready(m);
    TEST_ASSERT_TRUE(m.due(0));
    m.check(0, 0, false, false);
    TEST_ASSERT_EQUAL(0, fake.calls);
    TEST_ASSERT_EQUAL(Status::NoNetwork, m.snapshot().status);
}

void test_settings_change_during_check_drops_that_result() {
    WeatherManager m;
    ready(m);
    TEST_ASSERT_TRUE(m.due(0));
    // configure() lands while the request is out; emulate by reconfiguring first
    // then completing a check that captured the old generation.
    m.configure(true, String("newkey"), true, 1, 2);
    m.check(0, 0, false, true);  // this check reads the new generation: valid
    TEST_ASSERT_EQUAL(Status::Ok, m.snapshot().status);
    TEST_ASSERT_NOT_NULL(strstr(fake.last_url, "appid=newkey"));
    // Reconfigure makes the next check due at once.
    m.configure(true, String("k2"), true, 1, 2);
    TEST_ASSERT_EQUAL(Status::Waiting, m.snapshot().status);
    TEST_ASSERT_TRUE(m.due(1));
}

void test_seq_bumps_on_every_result() {
    WeatherManager m;
    ready(m);
    const uint32_t a = m.snapshot().seq;
    TEST_ASSERT_TRUE(m.due(0));
    m.check(0, 0, false, true);
    TEST_ASSERT_TRUE(m.snapshot().seq > a);
}

void test_geocode_paths() {
    WeatherManager m;
    ready(m);
    WeatherApi::Matches out;
    fake.body = "[{\"name\":\"Millburn\",\"lat\":40.72,\"lon\":-74.3,\"country\":\"US\",\"state\":\"New Jersey\"}]";
    TEST_ASSERT_EQUAL(Status::Ok, m.geocode(false, "Millburn,NJ", "US", out));
    TEST_ASSERT_EQUAL_UINT32(1, out.count);
    TEST_ASSERT_NOT_NULL(strstr(fake.last_url, "/geo/1.0/direct?q=Millburn%2CNJ%2CUS&limit=5"));
    fake.http_status = 404;
    fake.body = "{\"cod\":\"404\",\"message\":\"not found\"}";
    TEST_ASSERT_EQUAL(Status::Ok, m.geocode(true, "00000", "US", out));
    TEST_ASSERT_EQUAL_UINT32(0, out.count);
    fake.http_status = 401;
    TEST_ASSERT_EQUAL(Status::KeyRejected, m.geocode(true, "07041", "US", out));
    fake.http_status = 404;  // 404 on direct geocoding is a service error
    TEST_ASSERT_EQUAL(Status::ServiceError, m.geocode(false, "x", "US", out));
    WeatherManager nokey;
    nokey.setHttpGet(fakeGet);
    nokey.configure(true, String(""), false, 0, 0);
    TEST_ASSERT_EQUAL(Status::NotSetUp, nokey.geocode(false, "x", "US", out));
}

void test_key_never_reaches_the_log() {
    Logger::resetRecentForTest();
    WeatherManager m;
    fake = Fake{};
    m.setHttpGet(fakeGet);
    m.configure(true, String("SecretKey99"), true, 1, 2);
    const int statuses[] = {200, 401, 500};
    const char *bodies[] = {kGood, "{}", "x"};
    for (int i = 0; i < 3; ++i) {
        fake.http_status = statuses[i];
        fake.body = bodies[i];
        TEST_ASSERT_TRUE(m.due(static_cast<uint32_t>(i) * 300000U));
        m.check(static_cast<uint32_t>(i) * 300000U, 0, false, true);
    }
    WeatherApi::Matches out;
    fake.http_status = 401;
    m.geocode(false, "x", "US", out);
    Logger::RecentEntry entries[64];
    const size_t n = Logger::copyRecent(entries, 64);
    TEST_ASSERT_TRUE(n >= 3);  // the failures were logged
    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_NULL_MESSAGE(strstr(entries[i].message, "SecretKey99"), entries[i].message);
        TEST_ASSERT_NULL_MESSAGE(strstr(entries[i].message, "appid"), entries[i].message);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_off_and_not_set_up_states_never_poll);
    RUN_TEST(test_set_up_waits_then_reads_and_logs_dt);
    RUN_TEST(test_next_check_lands_on_the_next_five_minute_mark);
    RUN_TEST(test_untrusted_clock_polls_every_five_minutes);
    RUN_TEST(test_due_survives_millis_wrap);
    RUN_TEST(test_each_failure_shows_at_once_and_drops_the_old_value);
    RUN_TEST(test_no_wifi_makes_no_request);
    RUN_TEST(test_settings_change_during_check_drops_that_result);
    RUN_TEST(test_seq_bumps_on_every_result);
    RUN_TEST(test_geocode_paths);
    RUN_TEST(test_key_never_reaches_the_log);
    return UNITY_END();
}
