// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <stdlib.h>
#include <time.h>

#include <vector>

#include "ui/ChartPeriod.h"
#include "ui/LongChartSeries.h"

using LongChartSeries::AddResult;
using LongChartSeries::Builder;
using LongChartSeries::Grid;

namespace {

constexpr uint32_t kHour = 3600;
constexpr uint32_t kStart = 1790827200; // a local month start

HistoryAggregate aggregate(double sum, uint32_t count, float low, float high, uint8_t source = 0) {
    HistoryAggregate a;
    a.sum = sum;
    a.count = count;
    a.min = low;
    a.max = high;
    a.min_epoch = kStart;
    a.max_epoch = kStart;
    a.source = source;
    return a;
}

Grid grid(uint32_t slot_seconds, uint32_t count) {
    Grid g;
    g.first_start = kStart;
    g.slot_seconds = slot_seconds;
    g.slot_count = count;
    return g;
}

int add(Builder &b, uint32_t hour, const HistoryAggregate &a) {
    return static_cast<int>(b.add(kStart + hour * kHour, kStart + (hour + 1) * kHour, a));
}

} // namespace

void setUp() {}
void tearDown() {}

void test_slots_covering_rounds_up() {
    TEST_ASSERT_EQUAL_UINT32(248, LongChartSeries::slotsCovering(kStart, kStart + 31 * 24 * kHour, 3 * kHour));
    TEST_ASSERT_EQUAL_UINT32(241, LongChartSeries::slotsCovering(kStart, kStart + 721 * kHour, 3 * kHour));
    TEST_ASSERT_EQUAL_UINT32(0, LongChartSeries::slotsCovering(kStart, kStart, kHour));
    TEST_ASSERT_EQUAL_UINT32(0, LongChartSeries::slotsCovering(kStart, kStart + kHour, 0));
}

void test_slot_middle() {
    const Grid g = grid(3 * kHour, 4);
    TEST_ASSERT_EQUAL_INT64(static_cast<int64_t>(kStart) + 5400, LongChartSeries::slotMiddle(g, 0));
    TEST_ASSERT_EQUAL_INT64(static_cast<int64_t>(kStart) + 3 * 10800 + 5400, LongChartSeries::slotMiddle(g, 3));
}

void test_three_hours_combine_exactly() {
    Builder b;
    TEST_ASSERT_TRUE(b.reset(grid(3 * kHour, 2), false, 0));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added), add(b, 0, aggregate(10.0 * 12, 12, 9, 11)));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added), add(b, 1, aggregate(20.0 * 4, 4, 18, 25)));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added), add(b, 2, aggregate(16.0 * 12, 12, 8, 17)));
    std::vector<ChartPlot::Sample> s;
    b.samples(s);
    TEST_ASSERT_EQUAL_UINT32(2, s.size());
    TEST_ASSERT_TRUE(s[0].valid);
    TEST_ASSERT_TRUE(s[0].has_range);
    // (120 + 80 + 192) / 28 = 14: the sample-weighted mean, not the mean of means.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 14.0f, s[0].mean);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 8.0f, s[0].low);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 25.0f, s[0].high);
    TEST_ASSERT_FALSE(s[1].valid); // no record: a gap
}

void test_records_off_the_grid_are_outside() {
    Builder b;
    TEST_ASSERT_TRUE(b.reset(grid(kHour, 2), false, 0));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Outside),
                      static_cast<int>(b.add(kStart - kHour, kStart, aggregate(1, 1, 1, 1))));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Outside), add(b, 2, aggregate(1, 1, 1, 1)));
    // A record straddling the start counts where its middle is.
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added),
                      static_cast<int>(b.add(kStart - 600, kStart + 1800, aggregate(1, 1, 1, 1))));
}

void test_empty_and_bad_records() {
    Builder b;
    TEST_ASSERT_TRUE(b.reset(grid(kHour, 2), false, 0));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Empty), add(b, 0, HistoryAggregate{}));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::BadRecord),
                      static_cast<int>(b.add(kStart + kHour, kStart, aggregate(1, 1, 1, 1))));
}

void test_other_gas_sensor_is_left_out() {
    Builder b;
    TEST_ASSERT_TRUE(b.reset(grid(kHour, 2), true, 5));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::WrongSource), add(b, 0, aggregate(1, 1, 1, 1, 1)));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added), add(b, 1, aggregate(2, 1, 2, 2, 5)));
    std::vector<ChartPlot::Sample> s;
    b.samples(s);
    TEST_ASSERT_FALSE(s[0].valid);
    TEST_ASSERT_TRUE(s[1].valid);
}

// The Year view puts each local day (23, 24 or 25 hours) on a 24-hour slot of
// a grid anchored at local midnight on January 1st: every day of a year with
// both daylight saving changes lands in its own slot.
void test_year_days_each_get_their_own_day_slot() {
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
    uint32_t start = 0;
    uint32_t end = 0;
    TEST_ASSERT_TRUE(ChartPeriod::localYearBounds(2026, start, end));
    constexpr uint32_t kDay = 24 * kHour;
    const uint32_t slots = LongChartSeries::slotsCovering(start, end, kDay);
    TEST_ASSERT_EQUAL_UINT32(365, slots);
    Grid g;
    g.first_start = start;
    g.slot_seconds = kDay;
    g.slot_count = slots;
    Builder b;
    TEST_ASSERT_TRUE(b.reset(g, false, 0));
    uint32_t day_start = start;
    for (uint32_t day = 0; day < slots; ++day) {
        tm local{};
        local.tm_year = 2026 - 1900;
        local.tm_mon = 0;
        local.tm_mday = static_cast<int>(day) + 2; // midnight after this day
        local.tm_isdst = -1;
        const uint32_t day_end = static_cast<uint32_t>(mktime(&local));
        TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added),
                          static_cast<int>(b.add(day_start, day_end, aggregate(day, 1, day, day))));
        day_start = day_end;
    }
    TEST_ASSERT_EQUAL_UINT32(end, day_start);
    std::vector<ChartPlot::Sample> s;
    b.samples(s);
    TEST_ASSERT_EQUAL_UINT32(slots, s.size());
    for (uint32_t day = 0; day < slots; ++day) {
        TEST_ASSERT_TRUE(s[day].valid);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, static_cast<float>(day), s[day].mean);
    }
}

void test_reset_rejects_empty_grids() {
    Builder b;
    TEST_ASSERT_FALSE(b.reset(grid(kHour, 0), false, 0));
    TEST_ASSERT_FALSE(b.reset(grid(0, 3), false, 0));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Outside), add(b, 0, aggregate(1, 1, 1, 1)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_slots_covering_rounds_up);
    RUN_TEST(test_slot_middle);
    RUN_TEST(test_three_hours_combine_exactly);
    RUN_TEST(test_records_off_the_grid_are_outside);
    RUN_TEST(test_empty_and_bad_records);
    RUN_TEST(test_other_gas_sensor_is_left_out);
    RUN_TEST(test_year_days_each_get_their_own_day_slot);
    RUN_TEST(test_reset_rejects_empty_grids);
    return UNITY_END();
}
