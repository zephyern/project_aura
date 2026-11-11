// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/SummaryAppender.h"

#include "core/Logger.h"
#include "modules/ForwardOnlyGuard.h"
#include "modules/LongHistoryFiles.h"

namespace SummaryAppender {
namespace {
constexpr const char *kTag = "History";
} // namespace

Result prepare(DailyHistoryStorage &storage, const char *path, uint32_t &last_start,
               uint32_t &last_end) {
    last_start = 0;
    last_end = 0;
    String header;
    QuarterHourRow::appendSpanHeader(header);
    size_t removed = 0;
    if (LongHistoryFiles::repairTail(storage, path, header.c_str(), removed) ==
        LongHistoryFiles::RepairResult::Failed) {
        return Result::Failed;
    }
    switch (ForwardOnlyGuard::lastSpan(storage, path, last_start, last_end)) {
        case ForwardOnlyGuard::LastResult::Found:
        case ForwardOnlyGuard::LastResult::NoRecords:
            return Result::Ready;
        case ForwardOnlyGuard::LastResult::Unsupported:
            return Result::BadFile;
        case ForwardOnlyGuard::LastResult::Failed:
            return Result::Failed;
    }
    return Result::Failed;
}

Result append(DailyHistoryStorage &storage, const char *path, const QuarterHourRow::SpanRow &row,
              uint32_t &last_start) {
    if (ForwardOnlyGuard::accept(last_start, row.start_epoch).verdict ==
        ForwardOnlyGuard::Verdict::ClockBehind) {
        LOGW(kTag, "%s: record %u not after the last record %u; not written", path,
             static_cast<unsigned>(row.start_epoch), static_cast<unsigned>(last_start));
        return Result::ClockBehind;
    }
    bool exists = false;
    size_t size = 0;
    if (!storage.fileInfo(path, exists, size)) return Result::Failed;
    String text;
    if (!exists || size == 0) QuarterHourRow::appendSpanHeader(text);
    const QuarterHourRow::EncodeResult encoded = QuarterHourRow::appendSpanRow(row, text);
    if (encoded != QuarterHourRow::EncodeResult::Ok) {
        LOGE(kTag, "%s: record %u could not be encoded (%u)", path,
             static_cast<unsigned>(row.start_epoch), static_cast<unsigned>(encoded));
        return Result::BadRecord;
    }
    if (!storage.appendText(path, text.c_str())) return Result::Failed;
    last_start = row.start_epoch;
    return Result::Appended;
}

} // namespace SummaryAppender
