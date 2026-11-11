// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <vector>

#include "ui/ChartBand.h"

using ChartBand::Frame;
using ChartBand::kNoValue;
using ChartBand::Point;
using ChartBand::Rect;
using ChartBand::Result;

namespace {

std::vector<Rect> g_rects;

void collect(const Rect &rect, void *) { g_rects.push_back(rect); }

// 11 pixels wide (x 10..20), 101 rows (y 0..100), values 0..100.
Frame frame(int64_t pos_last) {
    Frame f;
    f.x0 = 10;
    f.width = 10;
    f.y0 = 0;
    f.height = 100;
    f.pos_first = 0;
    f.pos_last = pos_last;
    f.value_min = 0;
    f.value_max = 100;
    return f;
}

Point point(int64_t pos, int32_t low, int32_t high) {
    Point p;
    p.pos = pos;
    p.low = low;
    p.high = high;
    return p;
}

Point gap(int64_t pos) {
    Point p;
    p.pos = pos;
    return p;
}

Result run(const std::vector<Point> &points, const Frame &f) {
    g_rects.clear();
    return ChartBand::forEachRect(points.data(), points.size(), f, collect, nullptr);
}

// Every drawn pixel once: no two rectangles overlap.
void assertNoOverlap() {
    for (size_t i = 0; i < g_rects.size(); ++i) {
        for (size_t j = i + 1; j < g_rects.size(); ++j) {
            const Rect &a = g_rects[i];
            const Rect &b = g_rects[j];
            const bool overlap = a.x1 <= b.x2 && b.x1 <= a.x2 && a.y1 <= b.y2 && b.y1 <= a.y2;
            TEST_ASSERT_FALSE(overlap);
        }
    }
}

} // namespace

void setUp() { g_rects.clear(); }
void tearDown() {}

void test_mapping_matches_lvgl() {
    const Frame f = frame(4);
    // lv_chart: x = x0 + (w * i) / (cnt - 1)
    TEST_ASSERT_EQUAL_INT32(10, ChartBand::xFor(f, 0));
    TEST_ASSERT_EQUAL_INT32(12, ChartBand::xFor(f, 1)); // 10 * 1 / 4 = 2
    TEST_ASSERT_EQUAL_INT32(20, ChartBand::xFor(f, 4));
    // lv_map(v, 0, 100, 100, 0), clamped
    TEST_ASSERT_EQUAL_INT32(100, ChartBand::yFor(f, 0));
    TEST_ASSERT_EQUAL_INT32(0, ChartBand::yFor(f, 100));
    TEST_ASSERT_EQUAL_INT32(75, ChartBand::yFor(f, 25));
    TEST_ASSERT_EQUAL_INT32(0, ChartBand::yFor(f, 250));
    TEST_ASSERT_EQUAL_INT32(100, ChartBand::yFor(f, -5));
}

void test_flat_band_is_one_rectangle() {
    const std::vector<Point> points = {point(0, 20, 40), point(1, 20, 40), point(2, 20, 40)};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Drawn), static_cast<int>(run(points, frame(2))));
    TEST_ASSERT_EQUAL_UINT32(1, g_rects.size());
    TEST_ASSERT_EQUAL_INT32(10, g_rects[0].x1);
    TEST_ASSERT_EQUAL_INT32(20, g_rects[0].x2);
    TEST_ASSERT_EQUAL_INT32(60, g_rects[0].y1); // high 40
    TEST_ASSERT_EQUAL_INT32(80, g_rects[0].y2); // low 20
}

void test_envelope_is_interpolated_between_points() {
    // 0 -> 10 px: high 0 -> 100, low 0 -> 0
    const std::vector<Point> points = {point(0, 0, 0), point(1, 0, 100)};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Drawn), static_cast<int>(run(points, frame(1))));
    TEST_ASSERT_EQUAL_UINT32(11, g_rects.size());
    for (int32_t i = 0; i <= 10; ++i) {
        TEST_ASSERT_EQUAL_INT32(10 + i, g_rects[i].x1);
        TEST_ASSERT_EQUAL_INT32(10 + i, g_rects[i].x2);
        TEST_ASSERT_EQUAL_INT32(100 - 10 * i, g_rects[i].y1);
        TEST_ASSERT_EQUAL_INT32(100, g_rects[i].y2);
    }
    assertNoOverlap();
}

void test_band_breaks_at_gaps_like_the_line() {
    const std::vector<Point> points = {point(0, 10, 20), point(1, 10, 20), gap(2), point(3, 50, 60), gap(4)};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Drawn), static_cast<int>(run(points, frame(4))));
    // 0..1 joined (x 10..12), the lone point 3 is one column (x 17)
    TEST_ASSERT_EQUAL_UINT32(2, g_rects.size());
    TEST_ASSERT_EQUAL_INT32(10, g_rects[0].x1);
    TEST_ASSERT_EQUAL_INT32(12, g_rects[0].x2);
    TEST_ASSERT_EQUAL_INT32(17, g_rects[1].x1);
    TEST_ASSERT_EQUAL_INT32(17, g_rects[1].x2);
    TEST_ASSERT_EQUAL_INT32(40, g_rects[1].y1);
    TEST_ASSERT_EQUAL_INT32(50, g_rects[1].y2);
}

void test_crowded_points_take_the_union_per_column() {
    // 41 points on 11 columns: about four per column, alternating spans.
    std::vector<Point> points;
    for (int64_t i = 0; i <= 40; ++i) points.push_back(i % 2 ? point(i, 10, 30) : point(i, 50, 70));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Drawn), static_cast<int>(run(points, frame(40))));
    assertNoOverlap();
    // Columns 10..19 hold both kinds of point; column 20 holds only the last (50..70).
    TEST_ASSERT_EQUAL_UINT32(2, g_rects.size());
    TEST_ASSERT_EQUAL_INT32(10, g_rects[0].x1);
    TEST_ASSERT_EQUAL_INT32(19, g_rects[0].x2);
    TEST_ASSERT_EQUAL_INT32(30, g_rects[0].y1); // high 70
    TEST_ASSERT_EQUAL_INT32(90, g_rects[0].y2); // low 10
    TEST_ASSERT_EQUAL_INT32(20, g_rects[1].x1);
    TEST_ASSERT_EQUAL_INT32(30, g_rects[1].y1);
    TEST_ASSERT_EQUAL_INT32(50, g_rects[1].y2);
}

void test_positions_in_seconds_align_with_the_line_axis() {
    // Line axis 1000..2000 s; band points at 1500 and 2000 s.
    Frame f = frame(0);
    f.pos_first = 1000;
    f.pos_last = 2000;
    const std::vector<Point> points = {point(1500, 0, 100), point(2000, 0, 100)};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Drawn), static_cast<int>(run(points, f)));
    TEST_ASSERT_EQUAL_UINT32(1, g_rects.size());
    TEST_ASSERT_EQUAL_INT32(15, g_rects[0].x1);
    TEST_ASSERT_EQUAL_INT32(20, g_rects[0].x2);
}

void test_no_values_is_empty() {
    const std::vector<Point> points = {gap(0), gap(1)};
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Empty), static_cast<int>(run(points, frame(1))));
    TEST_ASSERT_EQUAL_UINT32(0, g_rects.size());
    TEST_ASSERT_EQUAL(static_cast<int>(Result::Empty), static_cast<int>(run({}, frame(1))));
}

void test_bad_frames_draw_nothing() {
    const std::vector<Point> points = {point(0, 1, 2), point(1, 1, 2)};
    Frame f = frame(1);
    f.width = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::BadFrame), static_cast<int>(run(points, f)));
    f = frame(0);
    TEST_ASSERT_EQUAL(static_cast<int>(Result::BadFrame), static_cast<int>(run(points, f)));
    f = frame(1);
    f.value_max = f.value_min;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::BadFrame), static_cast<int>(run(points, f)));
    TEST_ASSERT_EQUAL_UINT32(0, g_rects.size());
}

void test_bad_points_draw_nothing() {
    TEST_ASSERT_EQUAL(static_cast<int>(Result::BadPoint),
                      static_cast<int>(run({point(0, 50, 40), point(1, 1, 2)}, frame(1))));
    Point half = point(0, 1, 2);
    half.high = kNoValue;
    TEST_ASSERT_EQUAL(static_cast<int>(Result::BadPoint), static_cast<int>(run({half, point(1, 1, 2)}, frame(1))));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::BadPoint),
                      static_cast<int>(run({point(1, 1, 2), point(0, 1, 2)}, frame(1))));
    TEST_ASSERT_EQUAL_UINT32(0, g_rects.size());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_mapping_matches_lvgl);
    RUN_TEST(test_flat_band_is_one_rectangle);
    RUN_TEST(test_envelope_is_interpolated_between_points);
    RUN_TEST(test_band_breaks_at_gaps_like_the_line);
    RUN_TEST(test_crowded_points_take_the_union_per_column);
    RUN_TEST(test_positions_in_seconds_align_with_the_line_axis);
    RUN_TEST(test_no_values_is_empty);
    RUN_TEST(test_bad_frames_draw_nothing);
    RUN_TEST(test_bad_points_draw_nothing);
    return UNITY_END();
}
