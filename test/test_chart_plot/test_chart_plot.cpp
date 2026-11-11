// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <cmath>

#include "ui/ChartPlot.h"

using ChartPlot::Options;
using ChartPlot::Sample;
using ChartPlot::Stats;

namespace {

Sample sample(float mean, float low, float high) {
    Sample s;
    s.valid = true;
    s.has_range = true;
    s.mean = mean;
    s.low = low;
    s.high = high;
    return s;
}

float toFahrenheit(float c, const void *) { return c * 9.0f / 5.0f + 32.0f; }

} // namespace

void setUp() {}
void tearDown() {}

void test_line_values_are_scaled_and_gaps_marked() {
    const Sample samples[] = {sample(21.04f, 20.0f, 22.0f), Sample{}, sample(22.06f, 21.5f, 23.0f)};
    int32_t line[3] = {};
    Options options;
    options.point_scale = 10.0f;
    const Stats stats = ChartPlot::plotLine(samples, 3, options, line);
    TEST_ASSERT_EQUAL_INT32(210, line[0]);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, line[1]);
    TEST_ASSERT_EQUAL_INT32(221, line[2]);
    TEST_ASSERT_TRUE(stats.has_values);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 21.04f, stats.min_value);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 22.06f, stats.max_value);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 22.06f, stats.latest_value);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 21.04f, stats.scale_min);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 22.06f, stats.scale_max);
}

void test_band_widens_only_the_scale() {
    const Sample samples[] = {sample(21.0f, 19.5f, 22.0f), sample(22.0f, 21.5f, 24.5f)};
    int32_t line[2] = {};
    ChartBand::Point band[2];
    Options options;
    options.point_scale = 10.0f;
    Stats stats = ChartPlot::plotLine(samples, 2, options, line);
    ChartPlot::plotBand(samples, 2, 0, 1, options, band, stats);
    TEST_ASSERT_EQUAL_INT64(0, band[0].pos);
    TEST_ASSERT_EQUAL_INT64(1, band[1].pos);
    TEST_ASSERT_EQUAL_INT32(195, band[0].low);
    TEST_ASSERT_EQUAL_INT32(220, band[0].high);
    TEST_ASSERT_EQUAL_INT32(245, band[1].high);
    // Badges keep the line's extremes; the axis covers the band.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 21.0f, stats.min_value);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 22.0f, stats.max_value);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 19.5f, stats.scale_min);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 24.5f, stats.scale_max);
}

void test_transform_applies_to_line_and_band() {
    const Sample samples[] = {sample(20.0f, 10.0f, 30.0f)};
    int32_t line[1] = {};
    ChartBand::Point band[1];
    Options options;
    options.transform = toFahrenheit;
    Stats stats = ChartPlot::plotLine(samples, 1, options, line);
    ChartPlot::plotBand(samples, 1, 5, 1, options, band, stats);
    TEST_ASSERT_EQUAL_INT32(68, line[0]);
    TEST_ASSERT_EQUAL_INT32(50, band[0].low);
    TEST_ASSERT_EQUAL_INT32(86, band[0].high);
    TEST_ASSERT_EQUAL_INT64(5, band[0].pos);
}

void test_negative_values_are_filtered_when_required() {
    const Sample samples[] = {sample(-1.0f, -2.0f, 0.0f), sample(3.0f, -0.5f, 4.0f), sample(2.0f, 1.0f, 3.0f)};
    int32_t line[3] = {};
    ChartBand::Point band[3];
    Options options;
    options.require_non_negative = true;
    Stats stats = ChartPlot::plotLine(samples, 3, options, line);
    ChartPlot::plotBand(samples, 3, 0, 1, options, band, stats);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, line[0]);
    TEST_ASSERT_EQUAL_INT32(3, line[1]);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, band[0].low);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, band[1].low); // its low is negative
    TEST_ASSERT_EQUAL_INT32(1, band[2].low);
}

void test_non_finite_values_are_gaps() {
    const Sample samples[] = {sample(NAN, 1.0f, 2.0f), sample(1.0f, INFINITY, 2.0f)};
    int32_t line[2] = {};
    ChartBand::Point band[2];
    Options options;
    Stats stats = ChartPlot::plotLine(samples, 2, options, line);
    ChartPlot::plotBand(samples, 2, 0, 1, options, band, stats);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, line[0]);
    TEST_ASSERT_EQUAL_INT32(1, line[1]);
    TEST_ASSERT_EQUAL_INT32(2, band[0].high);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, band[1].low);
}

void test_sample_without_range_has_line_but_no_band() {
    Sample legacy = sample(5.0f, 0.0f, 0.0f);
    legacy.has_range = false;
    const Sample samples[] = {legacy};
    int32_t line[1] = {};
    ChartBand::Point band[1];
    Options options;
    Stats stats = ChartPlot::plotLine(samples, 1, options, line);
    ChartPlot::plotBand(samples, 1, 0, 1, options, band, stats);
    TEST_ASSERT_EQUAL_INT32(5, line[0]);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, band[0].low);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, stats.scale_min);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, stats.scale_max);
}

void test_no_values_reports_none() {
    const Sample samples[] = {Sample{}, Sample{}};
    int32_t line[2] = {};
    ChartBand::Point band[2];
    Options options;
    Stats stats = ChartPlot::plotLine(samples, 2, options, line);
    ChartPlot::plotBand(samples, 2, 0, 1, options, band, stats);
    TEST_ASSERT_FALSE(stats.has_values);
    TEST_ASSERT_TRUE(std::isnan(stats.latest_value));
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, line[0]);
    TEST_ASSERT_EQUAL_INT32(ChartBand::kNoValue, band[1].low);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_line_values_are_scaled_and_gaps_marked);
    RUN_TEST(test_band_widens_only_the_scale);
    RUN_TEST(test_transform_applies_to_line_and_band);
    RUN_TEST(test_negative_values_are_filtered_when_required);
    RUN_TEST(test_non_finite_values_are_gaps);
    RUN_TEST(test_sample_without_range_has_line_but_no_band);
    RUN_TEST(test_no_values_reports_none);
    return UNITY_END();
}
