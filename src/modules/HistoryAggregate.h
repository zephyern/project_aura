// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// Exact running statistics for one metric over one time bucket.
//
// Long-term history keeps the exact sum and sample count (not a rounded mean)
// so that any coarser bucket built from finer ones has the same mean as one
// computed directly from the raw samples. The time of the low and of the high
// is kept with them.
struct HistoryAggregate {
    double sum = 0.0;
    uint32_t count = 0;
    float min = 0.0f;
    float max = 0.0f;
    uint32_t min_epoch = 0; // UTC second of the first sample equal to min.
    uint32_t max_epoch = 0; // UTC second of the first sample equal to max.
    // Metric-specific provenance. Two non-empty aggregates with different
    // sources describe different quantities and must never be combined
    // (for example two different optional gas sensors).
    uint8_t source = 0;

    bool empty() const { return count == 0; }
    double mean() const { return sum / static_cast<double>(count); }
};

enum class HistoryCombineResult : uint8_t {
    Combined,
    SourceMismatch, // `into` is unchanged.
    CountOverflow,  // `into` is unchanged.
};

enum class HistoryMergeResult : uint8_t {
    Combined,
    Replaced,      // sources differed: `into` now holds `add` alone
    CountOverflow, // `into` is unchanged
};

namespace HistoryAggregates {

// Folds `add` into `into`. An empty side contributes nothing. When both sides
// reach the same low (or high), the earlier time is kept.
HistoryCombineResult combine(HistoryAggregate &into, const HistoryAggregate &add);

// The long-term history rule for data arriving in time order: combine, but
// when the source changed (another optional gas sensor), the newer data
// replaces the older instead of being mixed with it.
HistoryMergeResult mergeNewer(HistoryAggregate &into, const HistoryAggregate &add);

} // namespace HistoryAggregates
