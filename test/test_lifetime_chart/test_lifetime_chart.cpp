// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include "ui/LifetimeChart.h"

using LifetimeChart::Levels;
using WeeklySummary::kAnchorEpoch;
using WeeklySummary::kWeekSeconds;

namespace {

constexpr uint32_t kDay = 86400;
constexpr uint32_t kFifteenYears = 5479 * kDay;

uint32_t weeksAfterAnchor(uint32_t weeks) { return kAnchorEpoch + weeks * kWeekSeconds; }

} // namespace

void setUp() {}
void tearDown() {}

void test_a_few_days_is_one_weekly_point() {
    Levels l;
    TEST_ASSERT_TRUE(LifetimeChart::chooseLevels(weeksAfterAnchor(44) + kDay, weeksAfterAnchor(44) + 3 * kDay, l));
    TEST_ASSERT_EQUAL_UINT32(1, l.line_weeks);
    TEST_ASSERT_EQUAL_UINT32(1, l.band_weeks);
    TEST_ASSERT_EQUAL_UINT32(weeksAfterAnchor(44), l.start);
    TEST_ASSERT_EQUAL_UINT32(weeksAfterAnchor(45), l.end);
}

void test_weekly_up_to_the_point_limit() {
    Levels l;
    // Weeks 0..299: exactly 300 points.
    TEST_ASSERT_TRUE(LifetimeChart::chooseLevels(kAnchorEpoch, weeksAfterAnchor(299) + kDay, l));
    TEST_ASSERT_EQUAL_UINT32(1, l.line_weeks);
    TEST_ASSERT_EQUAL_UINT32(LifetimeChart::kMaxLinePoints, (l.end - l.start) / kWeekSeconds);
    // One week more needs two-week points, with a weekly band.
    TEST_ASSERT_TRUE(LifetimeChart::chooseLevels(kAnchorEpoch, weeksAfterAnchor(300) + kDay, l));
    TEST_ASSERT_EQUAL_UINT32(2, l.line_weeks);
    TEST_ASSERT_EQUAL_UINT32(1, l.band_weeks);
    TEST_ASSERT_EQUAL_UINT32(151, (l.end - l.start) / (2 * kWeekSeconds));
}

void test_coarser_levels_double_and_stay_aligned() {
    Levels l;
    // Fifteen years from the anchor: 783 weeks -> 4-week line (196 points), 2-week band.
    TEST_ASSERT_TRUE(LifetimeChart::chooseLevels(kAnchorEpoch, kAnchorEpoch + kFifteenYears, l));
    TEST_ASSERT_EQUAL_UINT32(4, l.line_weeks);
    TEST_ASSERT_EQUAL_UINT32(2, l.band_weeks);
    TEST_ASSERT_EQUAL_UINT32(0, (l.start - kAnchorEpoch) % (4 * kWeekSeconds));
    TEST_ASSERT_EQUAL_UINT32(0, (l.end - kAnchorEpoch) % (4 * kWeekSeconds));
    TEST_ASSERT_EQUAL_UINT32(196, (l.end - l.start) / (4 * kWeekSeconds));
    // A start in the middle of a 4-week bucket begins at that bucket.
    TEST_ASSERT_TRUE(LifetimeChart::chooseLevels(weeksAfterAnchor(5), weeksAfterAnchor(5) + kFifteenYears, l));
    TEST_ASSERT_EQUAL_UINT32(4, l.line_weeks);
    TEST_ASSERT_EQUAL_UINT32(weeksAfterAnchor(4), l.start);
}

void test_records_before_the_anchor_start_at_the_anchor() {
    Levels l;
    TEST_ASSERT_TRUE(LifetimeChart::chooseLevels(kAnchorEpoch - 30 * kDay, kAnchorEpoch + kDay, l));
    TEST_ASSERT_EQUAL_UINT32(kAnchorEpoch, l.start);
}

void test_impossible_times_are_refused() {
    Levels l;
    TEST_ASSERT_FALSE(LifetimeChart::chooseLevels(kAnchorEpoch, kAnchorEpoch - 1, l));
    TEST_ASSERT_FALSE(LifetimeChart::chooseLevels(weeksAfterAnchor(10), weeksAfterAnchor(9), l));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_few_days_is_one_weekly_point);
    RUN_TEST(test_weekly_up_to_the_point_limit);
    RUN_TEST(test_coarser_levels_double_and_stay_aligned);
    RUN_TEST(test_records_before_the_anchor_start_at_the_anchor);
    RUN_TEST(test_impossible_times_are_refused);
    return UNITY_END();
}
