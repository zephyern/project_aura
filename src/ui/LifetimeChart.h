// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "modules/WeeklySummary.h"

// The Lifetime chart range (docs/CHARTS_HISTORY.md): everything recorded, from
// the week ladder. The line uses the finest ladder level that needs no more
// than kMaxLinePoints points; the band uses the next finer level (one week
// when the line is already weekly), so it shows twice the detail.
namespace LifetimeChart {

// The same bound the ladder keeps per level, so the chosen line level always
// exists on the card once its data does.
constexpr uint32_t kMaxLinePoints = WeeklySummary::kMaxPointsPerLevel;

struct Levels {
    uint32_t line_weeks = 0;
    uint32_t band_weeks = 0;
    uint32_t start = 0; // UTC start of the first line bucket
    uint32_t end = 0;   // UTC end of the bucket holding `now`
};

// `first`: UTC start of the oldest record (records before the ladder's
// anchor are not in the ladder, so the chart starts at the anchor). False if
// `now` is before the anchor or before `first`.
bool chooseLevels(uint32_t first, uint32_t now, Levels &out);

} // namespace LifetimeChart
