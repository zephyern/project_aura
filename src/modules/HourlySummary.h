// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "modules/DailyHistoryStorage.h"
#include "modules/LongHistoryFiles.h"
#include "modules/SummaryWriter.h"

// Hourly summaries: one record per UTC hour in /aura/history/hourly/YYYY-MM.csv
// (UTC year and month), built from the 15-minute rows (docs/LONG_HISTORY.md).
namespace HourlySummary {

constexpr uint32_t kHourSeconds = 3600;

class Level : public SummaryLevel {
public:
    const char *name() const override { return "hourly"; }
    bool bucketFor(uint32_t quarter_start, Bucket &out) const override;
};

enum class ReadResult : uint8_t { Read, Failed };
struct ReadStats {
    uint32_t records = 0;
    uint32_t files_read = 0;
    uint32_t skipped = 0; // bad lines (logged)
};
// Every hourly record starting in [start, end), in time order, from the one or
// more UTC month files the range touches (a local month touches up to two).
ReadResult readRange(const DailyHistoryStorage &storage, uint32_t start, uint32_t end,
                     LongHistoryFiles::SpanFn fn, void *context, ReadStats &stats);

} // namespace HourlySummary
