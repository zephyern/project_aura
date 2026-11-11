// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "modules/DailyHistoryStorage.h"
#include "modules/QuarterHourRow.h"

// Files of the long-term history on the SD card (docs/LONG_HISTORY.md).
namespace LongHistoryFiles {

// /aura/history/15min/YYYY/MM/YYYY-MM-DD.csv, named by UTC date.
constexpr const char *kQuarterRoot = "/aura/history/15min";
// /aura/history/hourly/YYYY-MM.csv, UTC hours, named by UTC year and month.
constexpr const char *kHourlyRoot = "/aura/history/hourly";
// /aura/history/days/YYYY.csv, one record per local calendar day, named by local year.
constexpr const char *kDailyRoot = "/aura/history/days";
// /aura/history/weeks/wN.csv, N-week UTC buckets (N = 1, 2, 4, ...).
constexpr const char *kWeeklyRoot = "/aura/history/weeks";
constexpr size_t kPathCapacity = 48;
// A row is about 1 KB with every metric present; the header is a little longer.
constexpr size_t kMaxLineBytes = 2048;

// False if `utc_day_key` is not a valid YYYYMMDD from 2020 on.
bool quarterFilePath(uint32_t utc_day_key, char (&out)[kPathCapacity]);

enum class FindResult : uint8_t { Found, None, Failed };
// Newest day file under kQuarterRoot by name. Entries that are not
// YYYY/MM/YYYY-MM-DD.csv (with matching year and month) are ignored.
FindResult findNewestQuarterFile(const DailyHistoryStorage &storage, uint32_t &utc_day_key);

enum class RepairResult : uint8_t {
    Absent,          // no such file
    Clean,           // ends with '\n' (or is empty); not rewritten
    Repaired,        // bytes after the last '\n' removed
    HeaderRewritten, // no complete line at all: replaced by `header`
    Failed,          // storage failure; see storage.lastFailureKind()
};
// Removes a torn last line, as a power cut during an append leaves it. Logs
// one line (file, bytes removed) when it changes the file. `header` includes
// its '\n'. Must run before anything appends to the file.
RepairResult repairTail(DailyHistoryStorage &storage, const char *path, const char *header,
                        size_t &removed_bytes);

enum class ReadResult : uint8_t {
    Read,      // header valid; every good row delivered
    Absent,    // no such file
    Empty,     // zero bytes
    BadHeader, // first line is not a supported header; no rows delivered
    Failed,    // storage failure or out of memory
};
struct ReadStats {
    uint32_t rows = 0;
    uint32_t skipped = 0; // bad lines skipped and logged
};
// `line_offset` is the byte offset of the row's first character in the file.
// Return false to stop reading.
using RowFn = bool (*)(const QuarterHourRow::Row &row, size_t line_offset, void *context);
// The shared reader of 15-minute files: checks the header, then delivers each
// decodable row in file order. A bad line in the middle (or an unterminated
// last line) is skipped and logged with its line number and reason.
ReadResult readQuarterFile(const DailyHistoryStorage &storage, const char *path, RowFn fn,
                           void *context, ReadStats &stats);

// The same reader for summary files (span layout: hourly, daily, weekly).
using SpanFn = bool (*)(const QuarterHourRow::SpanRow &row, size_t line_offset, void *context);
ReadResult readSpanFile(const DailyHistoryStorage &storage, const char *path, SpanFn fn,
                        void *context, ReadStats &stats);

enum class ScanResult : uint8_t { Done, Stopped, Failed };
struct ScanStats {
    uint32_t files_read = 0;
    uint32_t files_unsupported = 0; // header not ours: skipped (logged)
    uint32_t rows_skipped = 0;      // bad lines skipped (logged)
};
// Every 15-minute row starting in [from, to), in time order, read day file by
// day file through readQuarterFile. Missing days are simply absent.
ScanResult scanQuarterRows(const DailyHistoryStorage &storage, uint32_t from, uint32_t to, RowFn fn,
                           void *context, ScanStats &stats);

// False outside 2020-2099 or for a month outside 1-12.
bool hourlyFilePath(uint32_t utc_year, uint32_t utc_month, char (&out)[kPathCapacity]);
// False outside 2020-2099.
bool dailyFilePath(uint32_t local_year, char (&out)[kPathCapacity]);
// False unless `weeks` is a power of two from 1 to 1024.
bool weeklyFilePath(uint32_t weeks, char (&out)[kPathCapacity]);

// Removes every file and directory under `root`, then `root` itself.
// Absent: nothing was there. Failed: see the storage's lastFailureKind();
// whatever was removed before the failure stays removed.
enum class TreeRemoveResult : uint8_t { Removed, Absent, Failed };
TreeRemoveResult removeTree(DailyHistoryStorage &storage, const char *root);

// Atomically drops every row starting at or after `cut` from a 15-minute file
// (`span` false) or a summary file (`span` true: rows whose end is after
// `cut`). Rows are in time order, so this keeps a prefix of the file.
enum class CutResult : uint8_t { Cut, Unchanged, Absent, Failed };
CutResult cutRowsFrom(DailyHistoryStorage &storage, const char *path, bool span, uint32_t cut);

} // namespace LongHistoryFiles
