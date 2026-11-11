// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "modules/DailyHistoryStorage.h"
#include "modules/QuarterHourRow.h"

// Appends summary records (hourly, daily, weekly files; span layout) with the
// rules every long-term file follows (docs/LONG_HISTORY.md): torn-tail repair
// before the first append, the forward-only rule, a header on a new file.
namespace SummaryAppender {

enum class Result : uint8_t {
    Ready,        // prepare(): the file can be appended to
    Appended,
    ClockBehind,  // record does not start after the file's last record
    BadRecord,    // the codec refused it (logged)
    BadFile,      // the file's header is not a supported summary header
    Failed,       // storage failure; see storage.lastFailureKind()
};

// Repairs the file's tail and reads its last record's start and end (both 0
// if none). BadFile when the header is not ours: the caller decides.
Result prepare(DailyHistoryStorage &storage, const char *path, uint32_t &last_start,
               uint32_t &last_end);

// Appends `row` to `path` if it starts after `last_start`, which it advances.
Result append(DailyHistoryStorage &storage, const char *path, const QuarterHourRow::SpanRow &row,
              uint32_t &last_start);

} // namespace SummaryAppender
