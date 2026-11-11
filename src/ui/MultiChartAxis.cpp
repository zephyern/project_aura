// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/MultiChartAxis.h"

#include <cmath>

#include "ui/UiControllerGraphsShared.h"

namespace MultiChartAxis {

namespace {

// The next round step after `step` (1, 2, 5, 10 times a power of ten).
constexpr float kNextStepFactor = 1.5f;
// Enough rounds to go from the first guess to one that holds the range.
constexpr int kMaxStepRounds = 8;

} // namespace

bool fit(float low, float high, Axis &out) {
    if (!std::isfinite(low) || !std::isfinite(high) || low > high) return false;
    float span = high - low;
    if (span <= 0.0f) {
        span = fabsf(high) * kFlatSpanFraction;
        if (span <= 0.0f) span = 1.0f;
        low -= span / 2.0f;
        high += span / 2.0f;
    }
    float step = graph_nice_step(span / kDivisions);
    float min = floorf(low / step) * step;
    for (int round = 0; round < kMaxStepRounds && min + step * kDivisions < high; ++round) {
        step = graph_nice_step(step * kNextStepFactor);
        min = floorf(low / step) * step;
    }
    if (min + step * kDivisions < high) return false;
    out.min = min;
    out.step = step;
    out.scale = powf(10.0f, ceilf(log10f(kMinCoordsPerStep / step)));
    const float exponent = floorf(log10f(step));
    out.decimals = exponent < 0.0f ? static_cast<uint8_t>(-exponent) : 0;
    return true;
}

} // namespace MultiChartAxis
