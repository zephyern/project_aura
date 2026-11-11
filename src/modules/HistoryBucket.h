// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "modules/ChartsHistory.h"
#include "modules/HistoryAggregate.h"
#include "modules/QuarterHourRow.h"

// The one derivation of every coarser long-term history bucket (hour, local
// day, week level, chart resampling) from finer records (docs/LONG_HISTORY.md).
// A bucket covers the UTC interval [start, end); only records lying wholly
// inside it count, in time order. Sums and counts add up exactly; low and high
// keep the earliest time they were reached.
namespace HistoryBucket {

// A bucket as stored in the summary files (start, exclusive end, metrics),
// plus how many finer records it combined.
struct Record : QuarterHourRow::SpanRow {
    uint32_t source_records = 0; // finer records combined into this one

    bool empty() const;
};

enum class AddResult : uint8_t {
    Added,
    Outside,    // not wholly inside [start, end): ignored
    OutOfOrder, // starts before the previous record ended: refused
    Invalid,    // empty or reversed interval: refused
};

struct Stats {
    uint32_t added = 0;
    uint32_t outside = 0;      // not in this bucket (normal when scanning a file)
    uint32_t refused = 0;      // OutOfOrder or Invalid (logged)
    uint32_t gas_replaced = 0; // optional gas type changed: earlier part dropped
    uint32_t overflows = 0;    // a metric's count would overflow: that part dropped
};

class Builder {
public:
    // False (and nothing changes) for an empty or reversed interval.
    bool begin(uint32_t start, uint32_t end);
    AddResult add(uint32_t start, uint32_t end,
                  const HistoryAggregate (&metrics)[ChartsHistory::kMetricCount]);
    AddResult add(const QuarterHourRow::Row &row);
    AddResult add(const Record &record);

    const Record &record() const { return record_; }
    const Stats &stats() const { return stats_; }

private:
    Record record_{};
    Stats stats_{};
    uint32_t last_end_ = 0;
    bool begun_ = false;
};

// Convenience over Builder for records already in memory (in time order).
bool deriveBucket(const QuarterHourRow::Row *rows, size_t count, uint32_t start, uint32_t end,
                  Record &out, Stats &stats);

} // namespace HistoryBucket
