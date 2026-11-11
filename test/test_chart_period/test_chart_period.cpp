// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui/ChartPeriod.h"

using ChartPeriod::Navigator;
using ChartPeriod::Step;

namespace {

void useZone(const char *tz) {
    setenv("TZ", tz, 1);
    tzset();
}

constexpr const char *kNewYork = "EST5EDT,M3.2.0,M11.1.0";

} // namespace

void setUp() { useZone(kNewYork); }
void tearDown() {}

void test_month_index_round_trips() {
    const int32_t oct = ChartPeriod::monthIndex(2026, 10);
    TEST_ASSERT_EQUAL_INT32(2026, ChartPeriod::monthYear(oct));
    TEST_ASSERT_EQUAL_INT32(10, ChartPeriod::monthOfYear(oct));
    TEST_ASSERT_EQUAL_INT32(ChartPeriod::monthIndex(2027, 1), oct + 3);
    TEST_ASSERT_EQUAL_INT32(oct, ChartPeriod::monthIndexOfDayKey(20261031));
    TEST_ASSERT_EQUAL_INT32(2026, ChartPeriod::yearOfDayKey(20261031));
}

void test_arrows_stop_at_oldest_and_current() {
    Navigator nav = ChartPeriod::make(10, 12, 12);
    TEST_ASSERT_FALSE(ChartPeriod::canStep(nav, Step::Forward)); // never into the future
    TEST_ASSERT_TRUE(ChartPeriod::step(nav, Step::Back));
    TEST_ASSERT_TRUE(ChartPeriod::step(nav, Step::Back));
    TEST_ASSERT_EQUAL_INT32(10, nav.current);
    TEST_ASSERT_FALSE(ChartPeriod::step(nav, Step::Back));
    TEST_ASSERT_EQUAL_INT32(10, nav.current);
    TEST_ASSERT_TRUE(ChartPeriod::step(nav, Step::Forward));
    TEST_ASSERT_EQUAL_INT32(11, nav.current);
}

void test_navigator_clamps_and_handles_one_period() {
    Navigator nav = ChartPeriod::make(10, 12, 40);
    TEST_ASSERT_EQUAL_INT32(12, nav.current);
    nav = ChartPeriod::make(10, 12, 3);
    TEST_ASSERT_EQUAL_INT32(10, nav.current);
    nav = ChartPeriod::make(15, 12, 12); // data dated after now
    TEST_ASSERT_EQUAL_INT32(12, nav.oldest);
    TEST_ASSERT_FALSE(ChartPeriod::canStep(nav, Step::Back));
    TEST_ASSERT_FALSE(ChartPeriod::canStep(nav, Step::Forward));
}

void test_local_month_bounds_follow_the_zone() {
    uint32_t start = 0;
    uint32_t end = 0;
    TEST_ASSERT_TRUE(ChartPeriod::localMonthBounds(ChartPeriod::monthIndex(2026, 10), start, end));
    TEST_ASSERT_EQUAL_UINT32(1790827200U, start); // 2026-10-01T00:00-04:00
    TEST_ASSERT_EQUAL_UINT32(31U * 86400U, end - start);
    // November has the fall-back day: 30 days plus one hour.
    uint32_t nov_start = 0;
    uint32_t nov_end = 0;
    TEST_ASSERT_TRUE(ChartPeriod::localMonthBounds(ChartPeriod::monthIndex(2026, 11), nov_start, nov_end));
    TEST_ASSERT_EQUAL_UINT32(end, nov_start);
    TEST_ASSERT_EQUAL_UINT32(30U * 86400U + 3600U, nov_end - nov_start);
    // December -> January crosses the year.
    TEST_ASSERT_TRUE(ChartPeriod::localMonthBounds(ChartPeriod::monthIndex(2026, 12), start, end));
    TEST_ASSERT_EQUAL_UINT32(31U * 86400U, end - start);
}

void test_local_year_bounds() {
    uint32_t start = 0;
    uint32_t end = 0;
    TEST_ASSERT_TRUE(ChartPeriod::localYearBounds(2026, start, end));
    TEST_ASSERT_EQUAL_UINT32(1767243600U, start); // 2026-01-01T00:00-05:00
    TEST_ASSERT_EQUAL_UINT32(365U * 86400U, end - start);
    TEST_ASSERT_TRUE(ChartPeriod::localYearBounds(2028, start, end));
    TEST_ASSERT_EQUAL_UINT32(366U * 86400U, end - start);
}

void test_period_at_time_uses_local_zone() {
    int32_t month = 0;
    int32_t year = 0;
    // 2027-01-01T03:00Z is still 2026-12-31 in New York.
    TEST_ASSERT_TRUE(ChartPeriod::localMonthIndexAt(1798772400, month));
    TEST_ASSERT_EQUAL_INT32(ChartPeriod::monthIndex(2026, 12), month);
    TEST_ASSERT_TRUE(ChartPeriod::localYearAt(1798772400, year));
    TEST_ASSERT_EQUAL_INT32(2026, year);
    useZone("UTC0");
    TEST_ASSERT_TRUE(ChartPeriod::localMonthIndexAt(1798772400, month));
    TEST_ASSERT_EQUAL_INT32(ChartPeriod::monthIndex(2027, 1), month);
}

void test_period_needs_trusted_time() {
    int32_t month = 0;
    int32_t year = 0;
    TEST_ASSERT_FALSE(ChartPeriod::localMonthIndexAt(1000, month));
    TEST_ASSERT_FALSE(ChartPeriod::localYearAt(1000, year));
}

void test_title_fills_both_tokens_in_either_order() {
    char out[32];
    TEST_ASSERT_TRUE(ChartPeriod::formatTitle("{month} {year}", "Oct", 2026, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Oct 2026", out);
    TEST_ASSERT_TRUE(ChartPeriod::formatTitle("{year}年{month}", "10月", 2026, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("2026年10月", out);
}

void test_title_rejects_bad_templates_and_short_buffers() {
    char out[8];
    TEST_ASSERT_FALSE(ChartPeriod::formatTitle("{month}", "Oct", 2026, out, sizeof(out)));
    TEST_ASSERT_FALSE(ChartPeriod::formatTitle("{year} {year} {month}", "Oct", 2026, out, sizeof(out)));
    TEST_ASSERT_FALSE(ChartPeriod::formatTitle("{month} {year}", "October", 2026, out, sizeof(out)));
    TEST_ASSERT_TRUE(ChartPeriod::formatTitle("{month} {year}", "Oct", 2026, out, 9));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_month_index_round_trips);
    RUN_TEST(test_arrows_stop_at_oldest_and_current);
    RUN_TEST(test_navigator_clamps_and_handles_one_period);
    RUN_TEST(test_local_month_bounds_follow_the_zone);
    RUN_TEST(test_local_year_bounds);
    RUN_TEST(test_period_at_time_uses_local_zone);
    RUN_TEST(test_period_needs_trusted_time);
    RUN_TEST(test_title_fills_both_tokens_in_either_order);
    RUN_TEST(test_title_rejects_bad_templates_and_short_buffers);
    return UNITY_END();
}
