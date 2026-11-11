// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <unity.h>

#include "ui/MultiChartAxis.h"

using MultiChartAxis::Axis;

void setUp() {}
void tearDown() {}

void test_axis_is_round_and_holds_the_range() {
    Axis a;
    TEST_ASSERT_TRUE(MultiChartAxis::fit(20.3f, 23.8f, a));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, a.step);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, a.min);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, a.max());
    TEST_ASSERT_EQUAL_UINT8(0, a.decimals);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, a.scale);
}

void test_step_grows_until_the_range_fits() {
    Axis a;
    // 405..1210 ppm: 200 would end at 1200, so the step becomes 500.
    TEST_ASSERT_TRUE(MultiChartAxis::fit(405.0f, 1210.0f, a));
    TEST_ASSERT_EQUAL_FLOAT(500.0f, a.step);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.min);
    TEST_ASSERT_TRUE(a.max() >= 1210.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, a.scale);
}

void test_small_steps_get_decimals_and_a_larger_scale() {
    Axis a;
    // 0.005 would end at 0.03, so the step becomes 0.01.
    TEST_ASSERT_TRUE(MultiChartAxis::fit(0.012f, 0.031f, a));
    TEST_ASSERT_EQUAL_FLOAT(0.01f, a.step);
    TEST_ASSERT_EQUAL_FLOAT(0.01f, a.min);
    TEST_ASSERT_EQUAL_UINT8(2, a.decimals);
    TEST_ASSERT_TRUE(MultiChartAxis::fit(0.012f, 0.029f, a));
    TEST_ASSERT_EQUAL_FLOAT(0.005f, a.step);
    TEST_ASSERT_EQUAL_UINT8(3, a.decimals);
    TEST_ASSERT_TRUE(a.step * a.scale >= MultiChartAxis::kMinCoordsPerStep);
    TEST_ASSERT_TRUE(a.step * a.scale < 10.0f * MultiChartAxis::kMinCoordsPerStep);
}

void test_flat_and_negative_values() {
    Axis a;
    TEST_ASSERT_TRUE(MultiChartAxis::fit(21.0f, 21.0f, a));
    TEST_ASSERT_TRUE(a.min < 21.0f && a.max() > 21.0f);
    TEST_ASSERT_TRUE(MultiChartAxis::fit(0.0f, 0.0f, a));
    TEST_ASSERT_TRUE(a.min < 0.0f && a.max() > 0.0f);
    TEST_ASSERT_TRUE(MultiChartAxis::fit(-7.5f, 3.2f, a));
    TEST_ASSERT_TRUE(a.min <= -7.5f && a.max() >= 3.2f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fmodf(a.min, a.step));
}

void test_bad_ranges_are_refused() {
    Axis a;
    TEST_ASSERT_FALSE(MultiChartAxis::fit(NAN, 1.0f, a));
    TEST_ASSERT_FALSE(MultiChartAxis::fit(0.0f, INFINITY, a));
    TEST_ASSERT_FALSE(MultiChartAxis::fit(2.0f, 1.0f, a));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_axis_is_round_and_holds_the_range);
    RUN_TEST(test_step_grows_until_the_range_fits);
    RUN_TEST(test_small_steps_get_decimals_and_a_larger_scale);
    RUN_TEST(test_flat_and_negative_values);
    RUN_TEST(test_bad_ranges_are_refused);
    return UNITY_END();
}
