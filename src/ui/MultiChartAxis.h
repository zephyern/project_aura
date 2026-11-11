// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// One Y axis of the multi-sensor chart (docs/CHARTS_HISTORY.md). Both axes
// always have kDivisions divisions, so each axis gets round tick values and
// both sit on the same grid lines.
namespace MultiChartAxis {

constexpr uint8_t kDivisions = 4;
// A flat line (all values equal) gets an axis this fraction of its value
// wide, so it sits in the middle instead of on an edge.
constexpr float kFlatSpanFraction = 0.1f;
// Chart values per tick step at least, so the drawn line keeps its detail.
constexpr float kMinCoordsPerStep = 100.0f;

struct Axis {
    float min = 0.0f;   // value of the bottom grid line
    float step = 0.0f;  // value between grid lines
    float scale = 1.0f; // display value -> chart value
    uint8_t decimals = 0; // decimals that print every tick exactly
    float max() const { return min + step * kDivisions; }
};

// The smallest round axis holding [low, high]. False when either is not
// finite or low > high.
bool fit(float low, float high, Axis &out);

} // namespace MultiChartAxis
