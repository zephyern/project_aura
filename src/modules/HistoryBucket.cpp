// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/HistoryBucket.h"

#include "core/Logger.h"

namespace HistoryBucket {
namespace {
constexpr const char *kTag = "History";
} // namespace

bool Record::empty() const {
    for (const HistoryAggregate &a : metrics) {
        if (!a.empty()) return false;
    }
    return true;
}

bool Builder::begin(uint32_t start, uint32_t end) {
    if (end <= start) return false;
    record_ = Record{};
    record_.start_epoch = start;
    record_.end_epoch = end;
    stats_ = Stats{};
    last_end_ = start;
    begun_ = true;
    return true;
}

AddResult Builder::add(uint32_t start, uint32_t end,
                       const HistoryAggregate (&metrics)[ChartsHistory::kMetricCount]) {
    AddResult result = AddResult::Added;
    if (!begun_ || end <= start) {
        result = AddResult::Invalid;
    } else if (start < record_.start_epoch || end > record_.end_epoch) {
        result = AddResult::Outside;
    } else if (start < last_end_) {
        result = AddResult::OutOfOrder;
    }
    if (result == AddResult::Outside) {
        ++stats_.outside;
        return result;
    }
    if (result != AddResult::Added) {
        ++stats_.refused;
        LOGW(kTag, "record [%u, %u) refused for bucket %u (%s)", static_cast<unsigned>(start),
             static_cast<unsigned>(end), static_cast<unsigned>(record_.start_epoch),
             result == AddResult::OutOfOrder ? "out of order" : "invalid");
        return result;
    }
    for (int m = 0; m < ChartsHistory::kMetricCount; ++m) {
        HistoryAggregate &into = record_.metrics[m];
        const uint8_t previous_source = into.source;
        switch (HistoryAggregates::mergeNewer(into, metrics[m])) {
        case HistoryMergeResult::Combined:
            break;
        case HistoryMergeResult::Replaced:
            ++stats_.gas_replaced;
            LOGW(kTag, "metric %d source changed from %u to %u in bucket %u; earlier part dropped", m,
                 static_cast<unsigned>(previous_source), static_cast<unsigned>(metrics[m].source),
                 static_cast<unsigned>(record_.start_epoch));
            break;
        case HistoryMergeResult::CountOverflow:
            ++stats_.overflows;
            LOGE(kTag, "metric %d count overflow in bucket %u; record at %u dropped for it", m,
                 static_cast<unsigned>(record_.start_epoch), static_cast<unsigned>(start));
            break;
        }
    }
    last_end_ = end;
    ++record_.source_records;
    ++stats_.added;
    return AddResult::Added;
}

AddResult Builder::add(const QuarterHourRow::Row &row) {
    if (row.start_epoch > UINT32_MAX - QuarterHourRow::kQuarterSeconds) {
        ++stats_.refused;
        LOGW(kTag, "quarter row at %u has no valid end; refused", static_cast<unsigned>(row.start_epoch));
        return AddResult::Invalid;
    }
    return add(row.start_epoch, row.start_epoch + QuarterHourRow::kQuarterSeconds, row.metrics);
}

AddResult Builder::add(const Record &record) {
    return add(record.start_epoch, record.end_epoch, record.metrics);
}

bool deriveBucket(const QuarterHourRow::Row *rows, size_t count, uint32_t start, uint32_t end,
                  Record &out, Stats &stats) {
    Builder builder;
    if (!builder.begin(start, end) || (count > 0 && !rows)) return false;
    for (size_t i = 0; i < count; ++i) (void)builder.add(rows[i]);
    out = builder.record();
    stats = builder.stats();
    return true;
}

} // namespace HistoryBucket
