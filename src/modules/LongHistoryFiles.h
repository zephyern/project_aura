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
// Return false to stop reading.
using RowFn = bool (*)(const QuarterHourRow::Row &row, void *context);
// The shared reader of 15-minute files: checks the header, then delivers each
// decodable row in file order. A bad line in the middle (or an unterminated
// last line) is skipped and logged with its line number and reason.
ReadResult readQuarterFile(const DailyHistoryStorage &storage, const char *path, RowFn fn,
                           void *context, ReadStats &stats);

} // namespace LongHistoryFiles
