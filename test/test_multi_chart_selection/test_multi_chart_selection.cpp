// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include <unity.h>

#include "ui/MultiChartSelection.h"

using MultiChartSelection::Axis;
using MultiChartSelection::Selection;
using MultiChartSelection::Series;
using MultiChartSelection::Toggle;

namespace {

// CO2 ppm, temperature C, humidity %, CO ppm, a second temperature (as layer
// E's outdoor series will be), VOC idx.
const Series kSeries[] = {
    {ChartsHistory::METRIC_CO2, "ppm"}, {ChartsHistory::METRIC_TEMPERATURE, "C"},
    {ChartsHistory::METRIC_HUMIDITY, "%"}, {ChartsHistory::METRIC_CO, "ppm"},
    {ChartsHistory::kMetricCount, "C"},   {ChartsHistory::METRIC_VOC, "idx"},
};
constexpr size_t kCount = sizeof(kSeries) / sizeof(kSeries[0]);
constexpr size_t kCo2 = 0, kTemp = 1, kHumidity = 2, kCo = 3, kOutdoor = 4, kVoc = 5;

Selection makeSelection() {
    Selection s;
    TEST_ASSERT_TRUE(s.init(kSeries, kCount));
    return s;
}

} // namespace

void setUp() {}
void tearDown() {}

void test_groups_follow_first_appearance_of_each_unit() {
    const Selection s = makeSelection();
    TEST_ASSERT_EQUAL_UINT32(4, s.groupCount());
    TEST_ASSERT_EQUAL_STRING("ppm", s.groupUnit(0));
    TEST_ASSERT_EQUAL_STRING("C", s.groupUnit(1));
    TEST_ASSERT_EQUAL_STRING("%", s.groupUnit(2));
    TEST_ASSERT_EQUAL_STRING("idx", s.groupUnit(3));
    TEST_ASSERT_EQUAL_UINT32(0, s.groupOf(kCo));
    TEST_ASSERT_EQUAL_UINT32(1, s.groupOf(kOutdoor));
    TEST_ASSERT_EQUAL_UINT32(3, s.groupOf(kVoc));
}

void test_same_unit_shares_one_axis() {
    Selection s = makeSelection();
    Axis axis = Axis::Right;
    TEST_ASSERT_EQUAL(Toggle::Added, s.toggle(kTemp));
    TEST_ASSERT_EQUAL(Toggle::Added, s.toggle(kOutdoor));
    TEST_ASSERT_TRUE(s.axisOf(kOutdoor, axis));
    TEST_ASSERT_EQUAL(Axis::Left, axis);
    TEST_ASSERT_EQUAL_STRING("C", s.axisUnit(Axis::Left));
    TEST_ASSERT_NULL(s.axisUnit(Axis::Right));
}

void test_a_third_unit_is_refused_and_nothing_changes() {
    Selection s = makeSelection();
    TEST_ASSERT_EQUAL(Toggle::Added, s.toggle(kCo2));
    TEST_ASSERT_EQUAL(Toggle::Added, s.toggle(kTemp));
    TEST_ASSERT_EQUAL(Toggle::ThirdUnit, s.toggle(kHumidity));
    TEST_ASSERT_FALSE(s.selected(kHumidity));
    TEST_ASSERT_EQUAL_STRING("ppm", s.axisUnit(Axis::Left));
    TEST_ASSERT_EQUAL_STRING("C", s.axisUnit(Axis::Right));
    // A unit already on an axis still fits.
    TEST_ASSERT_EQUAL(Toggle::Added, s.toggle(kCo));
}

void test_hiding_keeps_the_series_and_its_axis() {
    Selection s = makeSelection();
    Axis axis = Axis::Left;
    s.toggle(kCo2);
    s.toggle(kTemp);
    TEST_ASSERT_TRUE(s.setHidden(kTemp, true));
    TEST_ASSERT_TRUE(s.selected(kTemp));
    TEST_ASSERT_FALSE(s.visible(kTemp));
    TEST_ASSERT_TRUE(s.axisOf(kTemp, axis));
    TEST_ASSERT_EQUAL(Axis::Right, axis);
    // The hidden unit still holds its axis.
    TEST_ASSERT_EQUAL(Toggle::ThirdUnit, s.toggle(kVoc));
    TEST_ASSERT_TRUE(s.setHidden(kTemp, false));
    TEST_ASSERT_TRUE(s.visible(kTemp));
    // Only selected series can be hidden.
    TEST_ASSERT_FALSE(s.setHidden(kHumidity, true));
    TEST_ASSERT_FALSE(s.setHidden(kCount, true));
}

void test_removing_the_last_series_of_a_unit_frees_only_its_axis() {
    Selection s = makeSelection();
    Axis axis = Axis::Left;
    s.toggle(kCo2);
    s.toggle(kCo);
    s.toggle(kTemp);
    TEST_ASSERT_EQUAL(Toggle::Removed, s.toggle(kCo2));
    TEST_ASSERT_EQUAL_STRING("ppm", s.axisUnit(Axis::Left)); // CO still selected
    s.setHidden(kCo, true);
    TEST_ASSERT_EQUAL(Toggle::Removed, s.toggle(kCo));
    TEST_ASSERT_FALSE(s.selected(kCo));
    TEST_ASSERT_NULL(s.axisUnit(Axis::Left));
    // Temperature stays on the right; the new unit takes the free left axis.
    TEST_ASSERT_EQUAL(Toggle::Added, s.toggle(kHumidity));
    TEST_ASSERT_TRUE(s.axisOf(kTemp, axis));
    TEST_ASSERT_EQUAL(Axis::Right, axis);
    TEST_ASSERT_TRUE(s.axisOf(kHumidity, axis));
    TEST_ASSERT_EQUAL(Axis::Left, axis);
    // A re-added series starts shown.
    TEST_ASSERT_EQUAL(Toggle::Removed, s.toggle(kHumidity));
    TEST_ASSERT_EQUAL(Toggle::Added, s.toggle(kCo));
    TEST_ASSERT_TRUE(s.visible(kCo));
}

void test_bad_input_is_refused() {
    Selection s;
    const Series no_unit[] = {{ChartsHistory::METRIC_CO2, "ppm"}, {ChartsHistory::METRIC_CO, nullptr}};
    TEST_ASSERT_FALSE(s.init(no_unit, 2));
    TEST_ASSERT_EQUAL_UINT32(0, s.seriesCount());
    TEST_ASSERT_FALSE(s.init(nullptr, 1));
    TEST_ASSERT_FALSE(s.init(kSeries, MultiChartSelection::kMaxSeries + 1));
    s = makeSelection();
    Axis axis = Axis::Left;
    TEST_ASSERT_EQUAL(Toggle::UnknownSeries, s.toggle(kCount));
    TEST_ASSERT_FALSE(s.selected(kCount));
    TEST_ASSERT_FALSE(s.axisOf(kCo2, axis));
}

void test_sensor_series_use_the_units_table_and_the_fitted_gas_unit() {
    Series series[MultiChartSelection::kMaxSeries];
    size_t count = 0;
    TEST_ASSERT_TRUE(MultiChartSelection::sensorSeries("ppb", series, MultiChartSelection::kMaxSeries, count));
    TEST_ASSERT_EQUAL_UINT32(ChartsHistory::kMetricCount, count);
    bool seen[ChartsHistory::kMetricCount] = {};
    for (size_t i = 0; i < count; ++i) {
        seen[series[i].id] = true;
        if (series[i].id == ChartsHistory::METRIC_OPTIONAL_GAS) TEST_ASSERT_EQUAL_STRING("ppb", series[i].unit);
        if (series[i].id == ChartsHistory::METRIC_TEMPERATURE) TEST_ASSERT_EQUAL_STRING("C", series[i].unit);
        if (series[i].id == ChartsHistory::METRIC_PM25) TEST_ASSERT_EQUAL_STRING("ug/m3", series[i].unit);
    }
    for (bool s : seen) TEST_ASSERT_TRUE(s);
    Selection s;
    TEST_ASSERT_TRUE(s.init(series, count));
    // ppm, C, %, hPa, idx, ppb, #/cm3, ug/m3 (HCHO and the ozone sensor share ppb).
    TEST_ASSERT_EQUAL_UINT32(8, s.groupCount());
    TEST_ASSERT_FALSE(MultiChartSelection::sensorSeries("ppb", series, count - 1, count));
    TEST_ASSERT_FALSE(MultiChartSelection::sensorSeries(nullptr, series, MultiChartSelection::kMaxSeries, count));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_groups_follow_first_appearance_of_each_unit);
    RUN_TEST(test_same_unit_shares_one_axis);
    RUN_TEST(test_a_third_unit_is_refused_and_nothing_changes);
    RUN_TEST(test_hiding_keeps_the_series_and_its_axis);
    RUN_TEST(test_removing_the_last_series_of_a_unit_frees_only_its_axis);
    RUN_TEST(test_bad_input_is_refused);
    RUN_TEST(test_sensor_series_use_the_units_table_and_the_fitted_gas_unit);
    return UNITY_END();
}
