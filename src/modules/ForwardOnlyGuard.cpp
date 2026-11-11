// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/ForwardOnlyGuard.h"

#include <stdio.h>
#include <string.h>

#include "core/LocalDate.h"
#include "core/Logger.h"
#include "modules/LongHistoryFiles.h"

namespace ForwardOnlyGuard {
namespace {

constexpr const char *kTag = "History";
constexpr size_t kMaxNames = 32; // entries handled per directory pass

bool takeLast(const QuarterHourRow::Row &row, size_t, void *context) {
    *static_cast<uint32_t *>(context) = row.start_epoch;
    return true;
}

struct LastSpan {
    uint32_t start = 0;
    uint32_t end = 0;
};

bool takeLastSpan(const QuarterHourRow::SpanRow &row, size_t, void *context) {
    LastSpan &last = *static_cast<LastSpan *>(context);
    last.start = row.start_epoch;
    last.end = row.end_epoch;
    return true;
}

LastResult lastFromRead(LongHistoryFiles::ReadResult read, const LongHistoryFiles::ReadStats &stats,
                        uint32_t last, uint32_t &last_epoch) {
    switch (read) {
        case LongHistoryFiles::ReadResult::Read:
            if (stats.rows == 0) return LastResult::NoRecords;
            last_epoch = last;
            return LastResult::Found;
        case LongHistoryFiles::ReadResult::Absent:
        case LongHistoryFiles::ReadResult::Empty:
            return LastResult::NoRecords;
        case LongHistoryFiles::ReadResult::BadHeader:
            return LastResult::Unsupported;
        case LongHistoryFiles::ReadResult::Failed:
            return LastResult::Failed;
    }
    return LastResult::Failed;
}

struct FirstFuture {
    uint32_t now = 0;
    bool found = false;
    size_t offset = 0;
    uint32_t rows_after = 0;
};

bool findFirstFuture(const QuarterHourRow::Row &row, size_t line_offset, void *context) {
    FirstFuture &f = *static_cast<FirstFuture *>(context);
    if (row.start_epoch > f.now) {
        if (!f.found) {
            f.found = true;
            f.offset = line_offset;
        }
        ++f.rows_after;
    }
    return true;
}

// Names (all digits plus an optional ".csv") newer than `after`, from one directory.
struct Names {
    size_t digits = 0;  // 4 = year, 2 = month, 14 = day file
    uint32_t after = 0; // keep only values greater than this
    uint32_t year = 0, month = 0;
    uint32_t values[kMaxNames] = {};
    size_t count = 0;
    bool overflow = false;
};

bool parseNumber(const char *text, size_t len, uint32_t &out) {
    uint32_t v = 0;
    for (size_t i = 0; i < len; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        v = v * 10U + static_cast<uint32_t>(text[i] - '0');
    }
    out = v;
    return true;
}

bool collectNewer(const char *name, bool is_dir, void *context) {
    Names &n = *static_cast<Names *>(context);
    const size_t len = strlen(name);
    uint32_t value = 0;
    if (n.digits == 14) {
        uint32_t y = 0, m = 0, d = 0;
        if (is_dir || len != 14 || name[4] != '-' || name[7] != '-' || strcmp(name + 10, ".csv") != 0 ||
            !parseNumber(name, 4, y) || !parseNumber(name + 5, 2, m) || !parseNumber(name + 8, 2, d) ||
            y != n.year || m != n.month) {
            return true;
        }
        value = y * 10000U + m * 100U + d;
    } else if (!is_dir || len != n.digits || !parseNumber(name, len, value)) {
        return true;
    }
    if (value <= n.after) return true;
    if (n.count == kMaxNames) {
        n.overflow = true;
        return false;
    }
    n.values[n.count++] = value;
    return true;
}

bool list(const DailyHistoryStorage &storage, const char *path, Names &names) {
    names.count = 0;
    names.overflow = false;
    return storage.listDir(path, &collectNewer, &names) != DirListResult::Failed;
}

bool removeDir(DailyHistoryStorage &storage, const char *path) {
    const DirRemoveResult r = storage.removeEmptyDir(path);
    return r != DirRemoveResult::Failed;
}

// Removes every day file later than `today` below one month directory.
bool clearMonth(DailyHistoryStorage &storage, uint32_t year, uint32_t month, uint32_t today,
                ClearStats &stats) {
    char month_path[LongHistoryFiles::kPathCapacity];
    snprintf(month_path, sizeof(month_path), "%s/%04u/%02u", LongHistoryFiles::kQuarterRoot,
             static_cast<unsigned>(year), static_cast<unsigned>(month));
    Names days;
    days.digits = 14;
    days.after = today;
    days.year = year;
    days.month = month;
    do {
        if (!list(storage, month_path, days)) return false;
        for (size_t i = 0; i < days.count; ++i) {
            char path[LongHistoryFiles::kPathCapacity];
            if (!LongHistoryFiles::quarterFilePath(days.values[i], path)) continue;
            if (!storage.removeFile(path)) return false;
            ++stats.files_removed;
        }
    } while (days.overflow);
    return removeDir(storage, month_path);
}

} // namespace

Decision accept(uint32_t last_epoch, uint32_t new_epoch) {
    Decision d;
    d.last_epoch = last_epoch;
    d.verdict = new_epoch > last_epoch ? Verdict::Accept : Verdict::ClockBehind;
    return d;
}

LastResult lastQuarterEpoch(const DailyHistoryStorage &storage, const char *path,
                            uint32_t &last_epoch) {
    uint32_t last = 0;
    LongHistoryFiles::ReadStats stats;
    const LongHistoryFiles::ReadResult read =
        LongHistoryFiles::readQuarterFile(storage, path, &takeLast, &last, stats);
    return lastFromRead(read, stats, last, last_epoch);
}

LastResult lastSpan(const DailyHistoryStorage &storage, const char *path, uint32_t &last_start,
                    uint32_t &last_end) {
    LastSpan last;
    LongHistoryFiles::ReadStats stats;
    const LongHistoryFiles::ReadResult read =
        LongHistoryFiles::readSpanFile(storage, path, &takeLastSpan, &last, stats);
    const LastResult result = lastFromRead(read, stats, last.start, last_start);
    if (result == LastResult::Found) last_end = last.end;
    return result;
}

ClearResult clearFutureQuarterRows(DailyHistoryStorage &storage, time_t now, ClearStats &stats) {
    stats = ClearStats{};
    uint32_t today = 0;
    if (!LocalDate::utcDayKey(now, today)) return ClearResult::Failed;
    const uint32_t this_year = today / 10000U, this_month = (today / 100U) % 100U;

    // Today's file: cut at the first row that starts after now.
    char today_path[LongHistoryFiles::kPathCapacity];
    if (!LongHistoryFiles::quarterFilePath(today, today_path)) return ClearResult::Failed;
    FirstFuture first;
    first.now = static_cast<uint32_t>(now);
    LongHistoryFiles::ReadStats read_stats;
    const LongHistoryFiles::ReadResult read =
        LongHistoryFiles::readQuarterFile(storage, today_path, &findFirstFuture, &first, read_stats);
    if (read == LongHistoryFiles::ReadResult::Failed || read == LongHistoryFiles::ReadResult::BadHeader) {
        return ClearResult::Failed;
    }
    if (first.found) {
        if (!storage.truncateAtomic(today_path, first.offset)) return ClearResult::Failed;
        stats.rows_removed = first.rows_after;
    }

    // Later days this month, later months this year, then later years.
    if (!clearMonth(storage, this_year, this_month, today, stats)) return ClearResult::Failed;
    char year_path[LongHistoryFiles::kPathCapacity];
    snprintf(year_path, sizeof(year_path), "%s/%04u", LongHistoryFiles::kQuarterRoot,
             static_cast<unsigned>(this_year));
    Names months;
    months.digits = 2;
    months.after = this_month;
    do {
        if (!list(storage, year_path, months)) return ClearResult::Failed;
        for (size_t i = 0; i < months.count; ++i) {
            if (!clearMonth(storage, this_year, months.values[i], 0, stats)) return ClearResult::Failed;
        }
    } while (months.overflow);
    Names years;
    years.digits = 4;
    years.after = this_year;
    do {
        const uint32_t removed_before = stats.files_removed;
        if (!list(storage, LongHistoryFiles::kQuarterRoot, years)) return ClearResult::Failed;
        for (size_t y = 0; y < years.count; ++y) {
            char later_year[LongHistoryFiles::kPathCapacity];
            snprintf(later_year, sizeof(later_year), "%s/%04u", LongHistoryFiles::kQuarterRoot,
                     static_cast<unsigned>(years.values[y]));
            Names later_months;
            later_months.digits = 2;
            do {
                if (!list(storage, later_year, later_months)) return ClearResult::Failed;
                for (size_t m = 0; m < later_months.count; ++m) {
                    if (!clearMonth(storage, years.values[y], later_months.values[m], 0, stats)) {
                        return ClearResult::Failed;
                    }
                }
            } while (later_months.overflow);
            if (!removeDir(storage, later_year)) return ClearResult::Failed;
        }
        // More than kMaxNames later years, and a pass that removed nothing
        // (only foreign files left): another pass would list the same years.
        if (years.overflow && stats.files_removed == removed_before) return ClearResult::Failed;
    } while (years.overflow);
    LOGW(kTag, "cleared future-dated history after %u: %u files, %u rows",
         static_cast<unsigned>(now), static_cast<unsigned>(stats.files_removed),
         static_cast<unsigned>(stats.rows_removed));
    return ClearResult::Cleared;
}

} // namespace ForwardOnlyGuard
