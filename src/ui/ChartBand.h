// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

// The faded min/max band drawn under a chart line (docs/CHARTS_HISTORY.md).
// Pure geometry: band points in chart value units become filled pixel
// columns, using the same x and y mapping as the LVGL line chart, so the
// band lines up with the line drawn over it. No LVGL types here.
namespace ChartBand {

// Same value as LV_CHART_POINT_NONE (checked where the two meet).
constexpr int32_t kNoValue = INT32_MAX;

// One band point. `pos` is in the units of Frame::pos_first/pos_last (an
// index or seconds); low/high are chart values (display value x point
// scale). low == kNoValue marks a gap: the band breaks there, as the line does.
struct Point {
    int64_t pos = 0;
    int32_t low = kNoValue;
    int32_t high = kNoValue;
};

// Where the chart line is drawn. The line's first point sits at x0 and its
// last at x0 + width (LVGL's mapping); value_max maps to y0 and value_min
// to y0 + height, values outside are clamped (lv_map).
struct Frame {
    int32_t x0 = 0;
    int32_t width = 0;
    int32_t y0 = 0;
    int32_t height = 0;
    int64_t pos_first = 0;
    int64_t pos_last = 0;
    int32_t value_min = 0;
    int32_t value_max = 0;
};

// Inclusive pixel rectangle.
struct Rect {
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;
};

using RectSink = void (*)(const Rect &rect, void *context);

// Drawn: at least one rectangle went to the sink. Empty: no band point has
// a value. BadFrame: zero size, pos_last <= pos_first or value_max <=
// value_min. BadPoint: a point with low > high, only one of low/high set,
// or positions going backwards. Nothing is drawn unless the result is Drawn.
enum class Result : uint8_t { Drawn, Empty, BadFrame, BadPoint };

// Columns are one pixel wide, the envelope interpolated linearly between
// neighbouring points that both have values (the line's segments), joined
// into one rectangle where neighbouring columns cover the same rows. Several
// points on one column (more points than pixels) give the union of their
// spans, so no pixel is drawn twice.
Result forEachRect(const Point *points, size_t count, const Frame &frame, RectSink sink, void *context);

// The LVGL mappings, exposed for tests and for the glue.
int32_t xFor(const Frame &frame, int64_t pos);
int32_t yFor(const Frame &frame, int32_t value);

} // namespace ChartBand
