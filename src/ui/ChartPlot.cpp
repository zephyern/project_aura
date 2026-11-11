// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/ChartPlot.h"

#include <cmath>

namespace ChartPlot {

namespace {

// Display value of one storage value, false when it must not be drawn.
bool display(float raw, const Options &options, float &out) {
    if (!std::isfinite(raw)) return false;
    out = options.transform ? options.transform(raw, options.context) : raw;
    if (!std::isfinite(out)) return false;
    return !options.require_non_negative || out >= 0.0f;
}

int32_t chartValue(float display_value, const Options &options) {
    return static_cast<int32_t>(lroundf(display_value * options.point_scale));
}

void widen(Stats &stats, float low, float high) {
    if (!stats.has_values) {
        // No line value: the band alone sets the scale.
        stats.scale_min = low;
        stats.scale_max = high;
        return;
    }
    if (low < stats.scale_min) stats.scale_min = low;
    if (high > stats.scale_max) stats.scale_max = high;
}

} // namespace

Stats plotLine(const Sample *samples, size_t count, const Options &options, int32_t *line) {
    Stats stats;
    for (size_t i = 0; i < count; ++i) {
        line[i] = ChartBand::kNoValue;
        float value = 0.0f;
        if (!samples[i].valid || !display(samples[i].mean, options, value)) continue;
        if (!stats.has_values) {
            stats.has_values = true;
            stats.min_value = value;
            stats.max_value = value;
        } else {
            if (value < stats.min_value) stats.min_value = value;
            if (value > stats.max_value) stats.max_value = value;
        }
        stats.latest_value = value;
        line[i] = chartValue(value, options);
    }
    stats.scale_min = stats.min_value;
    stats.scale_max = stats.max_value;
    return stats;
}

void plotBand(const Sample *samples, size_t count, int64_t pos_first, int64_t pos_step, const Options &options,
              ChartBand::Point *band, Stats &stats) {
    bool any = false;
    float band_min = 0.0f;
    float band_max = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        ChartBand::Point &p = band[i];
        p = ChartBand::Point{};
        p.pos = pos_first + static_cast<int64_t>(i) * pos_step;
        float low = 0.0f;
        float high = 0.0f;
        if (!samples[i].valid || !samples[i].has_range || !display(samples[i].low, options, low) || !display(samples[i].high, options, high)) {
            continue;
        }
        p.low = chartValue(low, options);
        p.high = chartValue(high, options);
        if (!any) {
            any = true;
            band_min = low;
            band_max = high;
        } else {
            if (low < band_min) band_min = low;
            if (high > band_max) band_max = high;
        }
    }
    if (any) widen(stats, band_min, band_max);
}

} // namespace ChartPlot
