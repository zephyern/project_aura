// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/HourlySummary.h"

#include <time.h>

#include "core/LocalDate.h"

namespace HourlySummary {
namespace {

// UTC start of the month containing `epoch` and of the month after it.
bool monthBounds(uint32_t epoch, uint32_t &month_start, uint32_t &next_start, uint32_t &year,
                 uint32_t &month) {
    uint32_t key = 0;
    if (!LocalDate::utcDayKey(static_cast<time_t>(epoch), key)) return false;
    year = key / 10000U;
    month = (key / 100U) % 100U;
    struct tm tm_utc = {};
    if (!LocalDate::gmtimeInto(static_cast<time_t>(epoch), tm_utc)) return false;
    const uint32_t into_month = static_cast<uint32_t>(tm_utc.tm_mday - 1) * 86400U +
                                static_cast<uint32_t>(tm_utc.tm_hour) * 3600U +
                                static_cast<uint32_t>(tm_utc.tm_min) * 60U +
                                static_cast<uint32_t>(tm_utc.tm_sec);
    month_start = epoch - into_month;
    static constexpr uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days = kDays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days = 29;
    next_start = month_start + days * 86400U;
    return true;
}

struct RangeFilter {
    uint32_t start;
    uint32_t end;
    LongHistoryFiles::SpanFn fn;
    void *context;
    uint32_t delivered;
    bool stopped;
};

bool filter(const QuarterHourRow::SpanRow &row, size_t offset, void *context) {
    RangeFilter &f = *static_cast<RangeFilter *>(context);
    if (row.start_epoch < f.start || row.start_epoch >= f.end) return true;
    ++f.delivered;
    if (!f.fn(row, offset, f.context)) {
        f.stopped = true;
        return false;
    }
    return true;
}

} // namespace

bool Level::bucketFor(uint32_t quarter_start, Bucket &out) const {
    uint32_t month_start = 0, next_start = 0, year = 0, month = 0;
    if (!monthBounds(quarter_start, month_start, next_start, year, month)) return false;
    if (!LongHistoryFiles::hourlyFilePath(year, month, out.path)) return false;
    out.start = quarter_start - quarter_start % kHourSeconds;
    out.end = out.start + kHourSeconds;
    out.file_start = month_start;
    return true;
}

ReadResult readRange(const DailyHistoryStorage &storage, uint32_t start, uint32_t end,
                     LongHistoryFiles::SpanFn fn, void *context, ReadStats &stats) {
    stats = ReadStats{};
    if (!fn) return ReadResult::Failed;
    RangeFilter f{start, end, fn, context, 0, false};
    uint32_t cursor = start;
    while (cursor < end && !f.stopped) {
        uint32_t month_start = 0, next_start = 0, year = 0, month = 0;
        char path[LongHistoryFiles::kPathCapacity];
        if (!monthBounds(cursor, month_start, next_start, year, month) ||
            !LongHistoryFiles::hourlyFilePath(year, month, path)) {
            return ReadResult::Failed;
        }
        LongHistoryFiles::ReadStats rs;
        switch (LongHistoryFiles::readSpanFile(storage, path, &filter, &f, rs)) {
            case LongHistoryFiles::ReadResult::Read:
                ++stats.files_read;
                stats.skipped += rs.skipped;
                break;
            case LongHistoryFiles::ReadResult::Absent:
            case LongHistoryFiles::ReadResult::Empty:
                break;
            case LongHistoryFiles::ReadResult::BadHeader:
            case LongHistoryFiles::ReadResult::Failed:
                return ReadResult::Failed;
        }
        cursor = next_start;
    }
    stats.records = f.delivered;
    return ReadResult::Read;
}

} // namespace HourlySummary
