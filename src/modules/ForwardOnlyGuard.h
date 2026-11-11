// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>
#include <time.h>

#include "modules/DailyHistoryStorage.h"

// Long-term history files only ever grow forward in time (docs/LONG_HISTORY.md).
// Every appender asks the guard before writing a record: a record that does not
// start after the file's last record is refused, so a clock that moved back can
// never write out of order or duplicate a record. A clock that moved forward
// simply leaves a gap.
namespace ForwardOnlyGuard {

enum class Verdict : uint8_t { Accept, ClockBehind };

struct Decision {
    Verdict verdict = Verdict::ClockBehind;
    uint32_t last_epoch = 0; // the file's last record, reported with ClockBehind
};

// `last_epoch` 0 means the file has no records yet.
Decision accept(uint32_t last_epoch, uint32_t new_epoch);

enum class LastResult : uint8_t { Found, NoRecords, Failed };
// Start of the last good row of a 15-minute file, read at boot after the
// torn-tail repair. Absent, empty and header-only files have NoRecords.
// A file with an unsupported header is Failed: it must not be appended to.
LastResult lastQuarterEpoch(const DailyHistoryStorage &storage, const char *path,
                            uint32_t &last_epoch);

enum class ClearResult : uint8_t { Cleared, Failed };
struct ClearStats {
    uint32_t files_removed = 0;
    uint32_t rows_removed = 0;
};
// "Clear future-dated history": removes every 15-minute row that starts after
// `now` (trusted UTC), for a clock that was once set far ahead. Whole later
// day files are removed; the file for `now`'s day is cut at its first future row.
ClearResult clearFutureQuarterRows(DailyHistoryStorage &storage, time_t now, ClearStats &stats);

} // namespace ForwardOnlyGuard
