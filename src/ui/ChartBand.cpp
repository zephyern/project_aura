// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/ChartBand.h"

namespace ChartBand {

namespace {

bool hasValue(const Point &p) { return p.low != kNoValue; }

bool validFrame(const Frame &f) {
    return f.width > 0 && f.height > 0 && f.pos_last > f.pos_first && f.value_max > f.value_min;
}

bool validPoints(const Point *points, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const Point &p = points[i];
        if ((p.low == kNoValue) != (p.high == kNoValue)) return false;
        if (hasValue(p) && p.low > p.high) return false;
        if (i > 0 && p.pos < points[i - 1].pos) return false;
    }
    return true;
}

// Collects one column at a time (union of spans landing on it) and joins
// equal neighbouring columns into one rectangle before handing it on.
// Columns outside [x_min, x_max] (band points beyond the line's first or last
// point) are dropped, like LVGL clips the line to the chart.
class Columns {
public:
    Columns(RectSink sink, void *context, int32_t x_min, int32_t x_max)
        : sink_(sink), context_(context), x_min_(x_min), x_max_(x_max) {}

    void add(int32_t x, int32_t top, int32_t bottom) {
        if (x < x_min_ || x > x_max_) return;
        if (top > bottom) {
            const int32_t t = top;
            top = bottom;
            bottom = t;
        }
        if (column_open_ && x == column_x_) {
            if (top < column_top_) column_top_ = top;
            if (bottom > column_bottom_) column_bottom_ = bottom;
            return;
        }
        closeColumn();
        column_open_ = true;
        column_x_ = x;
        column_top_ = top;
        column_bottom_ = bottom;
    }

    // Returns true when anything was drawn.
    bool finish() {
        closeColumn();
        flushRun();
        return drawn_;
    }

private:
    void closeColumn() {
        if (!column_open_) return;
        column_open_ = false;
        if (run_open_ && column_x_ == run_.x2 + 1 && column_top_ == run_.y1 && column_bottom_ == run_.y2) {
            run_.x2 = column_x_;
            return;
        }
        flushRun();
        run_open_ = true;
        run_.x1 = column_x_;
        run_.x2 = column_x_;
        run_.y1 = column_top_;
        run_.y2 = column_bottom_;
    }

    void flushRun() {
        if (!run_open_) return;
        run_open_ = false;
        sink_(run_, context_);
        drawn_ = true;
    }

    RectSink sink_;
    void *context_;
    int32_t x_min_;
    int32_t x_max_;
    bool column_open_ = false;
    int32_t column_x_ = 0;
    int32_t column_top_ = 0;
    int32_t column_bottom_ = 0;
    bool run_open_ = false;
    Rect run_;
    bool drawn_ = false;
};

int32_t interpolate(int32_t a, int32_t b, int32_t step, int32_t steps) {
    return a + static_cast<int32_t>((static_cast<int64_t>(b - a) * step) / steps);
}

} // namespace

int32_t xFor(const Frame &frame, int64_t pos) {
    return frame.x0 + static_cast<int32_t>((static_cast<int64_t>(frame.width) * (pos - frame.pos_first)) /
                                           (frame.pos_last - frame.pos_first));
}

int32_t yFor(const Frame &frame, int32_t value) {
    // lv_map(value, value_min, value_max, y0 + height, y0) for value_max > value_min.
    const int32_t bottom = frame.y0 + frame.height;
    if (value >= frame.value_max) return frame.y0;
    if (value <= frame.value_min) return bottom;
    return static_cast<int32_t>((static_cast<int64_t>(value - frame.value_min) * (frame.y0 - bottom)) /
                                (static_cast<int64_t>(frame.value_max) - frame.value_min)) +
           bottom;
}

Result forEachRect(const Point *points, size_t count, const Frame &frame, RectSink sink, void *context) {
    if (!validFrame(frame)) return Result::BadFrame;
    if (count == 0) return Result::Empty;
    if (!points || !sink) return Result::BadPoint;
    if (!validPoints(points, count)) return Result::BadPoint;

    Columns columns(sink, context, frame.x0, frame.x0 + frame.width);
    for (size_t i = 0; i < count; ++i) {
        const Point &a = points[i];
        if (!hasValue(a)) continue;
        const int32_t xa = xFor(frame, a.pos);
        const int32_t top_a = yFor(frame, a.high);
        const int32_t bottom_a = yFor(frame, a.low);
        const bool joined = i + 1 < count && hasValue(points[i + 1]);
        if (!joined) {
            // End of a run (or a lone point): its own column.
            columns.add(xa, top_a, bottom_a);
            continue;
        }
        const Point &b = points[i + 1];
        const int32_t xb = xFor(frame, b.pos);
        const int32_t steps = xb - xa;
        if (steps == 0) {
            columns.add(xa, top_a, bottom_a);
            continue;
        }
        const int32_t top_b = yFor(frame, b.high);
        const int32_t bottom_b = yFor(frame, b.low);
        // Columns xa .. xb-1; column xb belongs to the next segment (or run end).
        for (int32_t step = 0; step < steps; ++step) {
            columns.add(xa + step, interpolate(top_a, top_b, step, steps), interpolate(bottom_a, bottom_b, step, steps));
        }
    }
    return columns.finish() ? Result::Drawn : Result::Empty;
}

} // namespace ChartBand
