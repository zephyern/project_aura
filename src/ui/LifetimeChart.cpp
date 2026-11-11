// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/LifetimeChart.h"

#include <algorithm>

namespace LifetimeChart {

bool chooseLevels(uint32_t first, uint32_t now, Levels &out) {
    const uint32_t from = std::max(first, WeeklySummary::kAnchorEpoch);
    if (now < from) return false;
    for (uint32_t weeks = 1; weeks <= WeeklySummary::kMaxLevelWeeks; weeks *= 2) {
        uint32_t start = 0;
        uint32_t unused = 0;
        uint32_t end = 0;
        if (!WeeklySummary::bucketBounds(weeks, from, start, unused) ||
            !WeeklySummary::bucketBounds(weeks, now, unused, end)) {
            return false;
        }
        const uint64_t points = (static_cast<uint64_t>(end) - start) / (static_cast<uint64_t>(weeks) *
                                                                         WeeklySummary::kWeekSeconds);
        if (points <= kMaxLinePoints) {
            out.line_weeks = weeks;
            out.band_weeks = weeks > 1 ? weeks / 2 : 1;
            out.start = start;
            out.end = end;
            return true;
        }
    }
    // 300 buckets of 1024 weeks is far beyond what a 32-bit time can reach.
    return false;
}

} // namespace LifetimeChart
