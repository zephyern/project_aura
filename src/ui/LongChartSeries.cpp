// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/LongChartSeries.h"

namespace LongChartSeries {

uint32_t slotsCovering(uint32_t start, uint32_t end, uint32_t slot_seconds) {
    if (end <= start || slot_seconds == 0) return 0;
    const uint32_t span = end - start;
    return span / slot_seconds + (span % slot_seconds != 0 ? 1U : 0U);
}

int64_t slotMiddle(const Grid &grid, uint32_t slot) {
    return static_cast<int64_t>(grid.first_start) + static_cast<int64_t>(slot) * grid.slot_seconds +
           grid.slot_seconds / 2;
}

bool Builder::reset(const Grid &grid, bool check_source, uint8_t source) {
    slots_.clear();
    if (grid.slot_count == 0 || grid.slot_seconds == 0) return false;
    grid_ = grid;
    check_source_ = check_source;
    source_ = source;
    slots_.assign(grid.slot_count, HistoryAggregate{});
    return true;
}

AddResult Builder::add(uint32_t start, uint32_t end, const HistoryAggregate &aggregate) {
    if (end <= start) return AddResult::BadRecord;
    if (aggregate.empty()) return AddResult::Empty;
    if (check_source_ && aggregate.source != source_) return AddResult::WrongSource;
    const uint32_t middle = start + (end - start) / 2;
    if (slots_.empty() || middle < grid_.first_start) return AddResult::Outside;
    const uint32_t slot = (middle - grid_.first_start) / grid_.slot_seconds;
    if (slot >= grid_.slot_count) return AddResult::Outside;
    switch (HistoryAggregates::combine(slots_[slot], aggregate)) {
        case HistoryCombineResult::Combined:
            return AddResult::Added;
        case HistoryCombineResult::SourceMismatch:
            return AddResult::SourceMismatch;
        case HistoryCombineResult::CountOverflow:
            return AddResult::CountOverflow;
    }
    return AddResult::BadRecord;
}

void Builder::samples(std::vector<ChartPlot::Sample> &out) const {
    out.assign(slots_.size(), ChartPlot::Sample{});
    for (size_t i = 0; i < slots_.size(); ++i) {
        const HistoryAggregate &a = slots_[i];
        if (a.empty()) continue;
        ChartPlot::Sample &s = out[i];
        s.valid = true;
        s.has_range = true;
        s.mean = static_cast<float>(a.mean());
        s.low = a.min;
        s.high = a.max;
    }
}

} // namespace LongChartSeries
