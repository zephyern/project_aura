// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <stdlib.h>
#include <time.h>

#include "config/AppConfig.h"
#include "core/LocalDate.h"

namespace {

// POSIX rule for US Eastern time, as the device stores it.
constexpr const char *kEastern = "EST5EDT,M3.2.0,M11.1.0";

void setTz(const char *tz) {
    setenv("TZ", tz, 1);
    tzset();
}

} // namespace

void setUp() { setTz(kEastern); }
void tearDown() { setTz("UTC0"); }

void test_local_and_utc_day_keys_differ_across_midnight() {
    uint32_t local = 0, utc = 0;
    // 2026-11-11T03:30:00Z is 22:30 on Nov 10 in New York (EST).
    TEST_ASSERT_TRUE(LocalDate::localDayKey(1794367800, local));
    TEST_ASSERT_TRUE(LocalDate::utcDayKey(1794367800, utc));
    TEST_ASSERT_EQUAL_UINT32(20261110, local);
    TEST_ASSERT_EQUAL_UINT32(20261111, utc);
}

void test_dst_days_keep_one_date_through_the_change() {
    uint32_t key = 0;
    // 2026-03-08: clocks go 02:00 EST -> 03:00 EDT (a 23-hour local day).
    TEST_ASSERT_TRUE(LocalDate::localDayKey(1772946000, key)); // 00:00 EST, 05:00Z
    TEST_ASSERT_EQUAL_UINT32(20260308, key);
    TEST_ASSERT_TRUE(LocalDate::localDayKey(1772946000 + 23 * 3600 - 1, key)); // 23:59:59 EDT
    TEST_ASSERT_EQUAL_UINT32(20260308, key);
    TEST_ASSERT_TRUE(LocalDate::localDayKey(1772946000 + 23 * 3600, key));
    TEST_ASSERT_EQUAL_UINT32(20260309, key);
    // 2026-11-01: clocks go 02:00 EDT -> 01:00 EST (a 25-hour local day).
    TEST_ASSERT_TRUE(LocalDate::localDayKey(1793505600, key)); // 00:00 EDT, 04:00Z
    TEST_ASSERT_EQUAL_UINT32(20261101, key);
    TEST_ASSERT_TRUE(LocalDate::localDayKey(1793505600 + 25 * 3600 - 1, key));
    TEST_ASSERT_EQUAL_UINT32(20261101, key);
    TEST_ASSERT_TRUE(LocalDate::localDayKey(1793505600 + 25 * 3600, key));
    TEST_ASSERT_EQUAL_UINT32(20261102, key);
    char buf[16];
    LocalDate::formatLocalTime(1793505600 + 25 * 3600 - 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("23:59:59", buf);
}

void test_untrusted_time_has_no_day_key() {
    uint32_t key = 12345;
    TEST_ASSERT_FALSE(LocalDate::localDayKey(Config::TIME_VALID_EPOCH, key));
    TEST_ASSERT_FALSE(LocalDate::utcDayKey(0, key));
    TEST_ASSERT_EQUAL_UINT32(12345, key);
    TEST_ASSERT_TRUE(LocalDate::utcDayKey(Config::TIME_VALID_EPOCH + 1, key));
    TEST_ASSERT_EQUAL_UINT32(20200101, key);
}

void test_formatting() {
    char buf[16] = "x";
    LocalDate::formatDayKey(20260308, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-03-08", buf);
    LocalDate::formatDayKey(20260308, nullptr, 4); // no crash
    LocalDate::formatDayKey(20260308, buf, 0);
    TEST_ASSERT_EQUAL_STRING("2026-03-08", buf);
    setTz("UTC0");
    LocalDate::formatLocalTime(1794367800, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("03:30:00", buf);
    tm t = {};
    TEST_ASSERT_TRUE(LocalDate::gmtimeInto(1794367800, t));
    TEST_ASSERT_EQUAL(126, t.tm_year);
    TEST_ASSERT_EQUAL(10, t.tm_mon);
    TEST_ASSERT_EQUAL(11, t.tm_mday);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_local_and_utc_day_keys_differ_across_midnight);
    RUN_TEST(test_dst_days_keep_one_date_through_the_change);
    RUN_TEST(test_untrusted_time_has_no_day_key);
    RUN_TEST(test_formatting);
    return UNITY_END();
}
