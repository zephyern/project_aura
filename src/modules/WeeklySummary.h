// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "modules/DailyHistoryStorage.h"
#include "modules/LongHistoryFiles.h"
#include "modules/SummaryWriter.h"

// The week ladder for the lifetime chart (docs/LONG_HISTORY.md):
// /aura/history/weeks/w1.csv holds one-week UTC buckets, w2.csv two-week
// buckets, w4.csv four-week buckets and so on, all anchored at Monday
// 2026-01-05 00:00 UTC so every coarser bucket is exactly two finer ones.
namespace WeeklySummary {

constexpr uint32_t kAnchorEpoch = 1767571200; // Monday 2026-01-05T00:00:00Z
constexpr uint32_t kWeekSeconds = 7U * 86400U;
// A coarser level is started once the finest level kept has more records
// than this, so the lifetime chart never needs more than about this many points.
constexpr uint32_t kMaxPointsPerLevel = 300;
constexpr uint32_t kMaxLevelWeeks = 1024;

// The one-week level, written from the 15-minute rows by SummaryWriter.
class Level : public SummaryLevel {
public:
    const char *name() const override { return "weekly"; }
    bool bucketFor(uint32_t quarter_start, Bucket &out) const override;
};

// [start, end) of the `weeks`-long bucket containing `epoch`. False before
// the anchor or for a `weeks` that is not a ladder level.
bool bucketBounds(uint32_t weeks, uint32_t epoch, uint32_t &start, uint32_t &end);

enum class LadderResult : uint8_t { Updated, Failed };
struct LadderStats {
    uint32_t levels = 1;          // levels present after the update (w1 counts)
    uint32_t records_written = 0; // coarser records appended by this update
};
// Builds every coarser level that is due from the level below it: a level of
// 2N weeks exists once level N has more than kMaxPointsPerLevel records, and
// is kept up to date with each complete 2N-week bucket. Coarser records are
// derived from the finer records through HistoryBucket, so they stay exact
// and survive the deletion of old 15-minute files.
LadderResult updateLadder(DailyHistoryStorage &storage, LadderStats &stats);

} // namespace WeeklySummary
