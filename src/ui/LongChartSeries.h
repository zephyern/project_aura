// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "modules/HistoryAggregate.h"
#include "ui/ChartPlot.h"

// Puts long-term history records (hourly, daily or week-ladder summaries) on
// a regular grid of chart slots (docs/CHARTS_HISTORY.md). A record belongs to
// the slot holding its middle; several records in one slot are combined
// exactly (HistoryAggregates::combine), so a 3-hour slot is the true 3-hour
// mean, low and high. Slots without a record stay gaps.
namespace LongChartSeries {

// What one long chart load covers: [start, end), the slot sizes of the line
// and the band, and the week-ladder level read for the band (Lifetime only).
struct Layout {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t line_slot_seconds = 0;
    uint32_t band_slot_seconds = 0;
    uint32_t band_weeks = 0;
};

struct Grid {
    uint32_t first_start = 0;  // UTC start of slot 0
    uint32_t slot_seconds = 0;
    uint32_t slot_count = 0;
};

// Slots needed to cover [start, end) in slots of `slot_seconds` (the last may
// be partial). 0 for an empty range.
uint32_t slotsCovering(uint32_t start, uint32_t end, uint32_t slot_seconds);

// UTC middle of slot `slot`, the position the chart uses for it.
int64_t slotMiddle(const Grid &grid, uint32_t slot);

enum class AddResult : uint8_t {
    Added,
    Empty,          // no samples for the metric: nothing to add
    Outside,        // the record's middle is not on the grid
    WrongSource,    // another optional gas sensor than the one on show
    SourceMismatch, // combine refused (cannot happen after the source check)
    CountOverflow,
    BadRecord,      // end not after start
};

class Builder {
public:
    // False for a grid with no slots or zero-length slots.
    bool reset(const Grid &grid, bool check_source, uint8_t source);
    AddResult add(uint32_t start, uint32_t end, const HistoryAggregate &aggregate);
    const Grid &grid() const { return grid_; }
    // One sample per slot in storage units (has_range always true).
    void samples(std::vector<ChartPlot::Sample> &out) const;

private:
    Grid grid_{};
    bool check_source_ = false;
    uint8_t source_ = 0;
    std::vector<HistoryAggregate> slots_;
};

} // namespace LongChartSeries
