// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/HistoryAggregate.h"

namespace HistoryAggregates {

HistoryCombineResult combine(HistoryAggregate &into, const HistoryAggregate &add) {
    if (add.empty()) {
        return HistoryCombineResult::Combined;
    }
    if (into.empty()) {
        into = add;
        return HistoryCombineResult::Combined;
    }
    if (into.source != add.source) {
        return HistoryCombineResult::SourceMismatch;
    }
    if (add.count > UINT32_MAX - into.count) {
        return HistoryCombineResult::CountOverflow;
    }
    into.sum += add.sum;
    into.count += add.count;
    if (add.min < into.min || (add.min == into.min && add.min_epoch < into.min_epoch)) {
        into.min = add.min;
        into.min_epoch = add.min_epoch;
    }
    if (add.max > into.max || (add.max == into.max && add.max_epoch < into.max_epoch)) {
        into.max = add.max;
        into.max_epoch = add.max_epoch;
    }
    return HistoryCombineResult::Combined;
}

} // namespace HistoryAggregates
