// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/DailySummary.h"

#include <time.h>

#include "core/LocalDate.h"

namespace DailySummary {
namespace {

// UTC epoch of local midnight at the start of the given local date. mktime
// normalises day overflow (day 32 -> next month) and finds the DST state.
bool localMidnight(int year, int month, int day, uint32_t &out) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_isdst = -1;
    const time_t epoch = mktime(&t);
    if (epoch <= 0) return false;
    out = static_cast<uint32_t>(epoch);
    return true;
}

} // namespace

bool Level::bucketFor(uint32_t quarter_start, Bucket &out) const {
    struct tm local = {};
    if (!LocalDate::localtimeInto(static_cast<time_t>(quarter_start), local)) return false;
    const int year = local.tm_year + 1900;
    uint32_t start = 0, end = 0, year_start = 0;
    if (!localMidnight(year, local.tm_mon + 1, local.tm_mday, start) ||
        !localMidnight(year, local.tm_mon + 1, local.tm_mday + 1, end) ||
        !localMidnight(year, 1, 1, year_start)) {
        return false;
    }
    if (start > quarter_start || quarter_start >= end) return false;
    if (!LongHistoryFiles::dailyFilePath(static_cast<uint32_t>(year), out.path)) return false;
    out.start = start;
    out.end = end;
    out.file_start = year_start;
    return true;
}

ReadResult readYear(const DailyHistoryStorage &storage, uint32_t year, LongHistoryFiles::SpanFn fn,
                    void *context, ReadStats &stats) {
    stats = ReadStats{};
    char path[LongHistoryFiles::kPathCapacity];
    if (!fn || !LongHistoryFiles::dailyFilePath(year, path)) return ReadResult::Failed;
    LongHistoryFiles::ReadStats rs;
    switch (LongHistoryFiles::readSpanFile(storage, path, fn, context, rs)) {
        case LongHistoryFiles::ReadResult::Read:
            stats.records = rs.rows;
            stats.skipped = rs.skipped;
            return ReadResult::Read;
        case LongHistoryFiles::ReadResult::Absent:
        case LongHistoryFiles::ReadResult::Empty:
            return ReadResult::Read;
        case LongHistoryFiles::ReadResult::BadHeader:
        case LongHistoryFiles::ReadResult::Failed:
            return ReadResult::Failed;
    }
    return ReadResult::Failed;
}

} // namespace DailySummary
