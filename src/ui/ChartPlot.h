// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "ui/ChartBand.h"

// Turns history samples into what a sensor chart draws (docs/CHARTS_HISTORY.md):
// the line's chart values, the band's points and the statistics the axis and
// the MIN/NOW/MAX badges use. One place for the conversion, the point scale
// and the "must not be negative" rule, so the line and the band always agree.
namespace ChartPlot {

// One history value in storage units (°C, hPa, ...). `low`/`high` are the
// smallest and largest reading inside the sample's interval; `has_range` says
// they are known (a legacy snapshot has a mean but no range).
struct Sample {
    bool valid = false;
    bool has_range = false;
    float mean = 0.0f;
    float low = 0.0f;
    float high = 0.0f;
};

// Storage value -> display value (°C/°F, hPa/inHg, ...). nullptr: unchanged.
using Transform = float (*)(float value, const void *context);

struct Options {
    Transform transform = nullptr;
    const void *context = nullptr;
    float point_scale = 1.0f;          // display value -> chart value (x10 for one decimal)
    bool require_non_negative = false; // negative display values are not drawn
};

struct Stats {
    bool has_values = false; // the line has at least one value
    float min_value = 0.0f;  // of the line (MIN badge)
    float max_value = 0.0f;  // of the line (MAX badge)
    float latest_value = NAN; // newest line value; NAN without one
    // What the axis must show: the line, widened by the band when one is drawn.
    float scale_min = 0.0f;
    float scale_max = 0.0f;
};

// Fills `line[count]` with chart values (ChartBand::kNoValue where a sample is
// missing, not finite or filtered out) and returns the line statistics.
Stats plotLine(const Sample *samples, size_t count, const Options &options, int32_t *line);

// Fills `band[count]`: sample i gets pos = pos_first + i * pos_step and its
// low/high as chart values, or a gap when it has no range or under the same
// rules as the line (both ends must pass). Widens stats.scale_min/scale_max by the band's extent.
void plotBand(const Sample *samples, size_t count, int64_t pos_first, int64_t pos_step, const Options &options,
              ChartBand::Point *band, Stats &stats);

} // namespace ChartPlot
