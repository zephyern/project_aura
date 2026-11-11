// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "modules/DailyHistoryStorage.h"
#include "modules/LongHistoryFiles.h"
#include "modules/SummaryWriter.h"

// Daily summaries: one record per LOCAL calendar day in
// /aura/history/days/YYYY.csv (local year), built from the 15-minute rows
// (docs/LONG_HISTORY.md). Each record stores its own UTC [start, end): every
// time zone the device offers is a whole number of quarter hours from UTC,
// so a local day is a whole number of quarters (96, or 92/100 on DST days).
namespace DailySummary {

class Level : public SummaryLevel {
public:
    const char *name() const override { return "daily"; }
    // Uses the time zone in effect now (TZ). A change of zone inside a day
    // ends that day at the next local midnight of the new zone (SummaryWriter).
    bool bucketFor(uint32_t quarter_start, Bucket &out) const override;
};

enum class ReadResult : uint8_t { Read, Failed };
struct ReadStats {
    uint32_t records = 0;
    uint32_t skipped = 0; // bad lines (logged)
};
// Every daily record of local year `year`, in time order. An absent file
// reads as no records.
ReadResult readYear(const DailyHistoryStorage &storage, uint32_t year, LongHistoryFiles::SpanFn fn,
                    void *context, ReadStats &stats);

} // namespace DailySummary
