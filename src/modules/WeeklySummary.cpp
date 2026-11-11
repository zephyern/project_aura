// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/WeeklySummary.h"

#include "core/Logger.h"
#include "modules/ForwardOnlyGuard.h"
#include "modules/HistoryBucket.h"
#include "modules/SummaryAppender.h"

namespace WeeklySummary {
namespace {

constexpr const char *kTag = "History";

bool isLevel(uint32_t weeks) {
    return weeks != 0 && weeks <= kMaxLevelWeeks && (weeks & (weeks - 1)) == 0;
}

struct Counter {
    uint32_t records = 0;
};
bool count(const QuarterHourRow::SpanRow &, size_t, void *context) {
    ++static_cast<Counter *>(context)->records;
    return true;
}

// Folds level-N records into 2N-week buckets and appends each complete one.
struct Folder {
    DailyHistoryStorage *storage;
    const char *path;
    uint32_t weeks; // of the coarser level
    uint32_t after; // end of the coarser file's last record
    uint32_t last_start;
    HistoryBucket::Builder builder;
    bool open = false;
    uint32_t written = 0;
    bool failed = false;
};

bool flush(Folder &f) {
    f.open = false;
    if (f.builder.record().empty()) return true;
    switch (SummaryAppender::append(*f.storage, f.path, f.builder.record(), f.last_start)) {
        case SummaryAppender::Result::Appended:
            ++f.written;
            return true;
        case SummaryAppender::Result::ClockBehind:
            return true; // already present; logged by the appender
        default:
            f.failed = true;
            return false;
    }
}

bool fold(const QuarterHourRow::SpanRow &row, size_t, void *context) {
    Folder &f = *static_cast<Folder *>(context);
    if (row.end_epoch <= f.after) return true;
    uint32_t start = 0, end = 0;
    if (!bucketBounds(f.weeks, row.start_epoch, start, end)) return true;
    if (f.open && start != f.builder.record().start_epoch) {
        if (!flush(f)) return false;
    }
    if (!f.open) {
        f.builder.begin(start, end);
        f.open = true;
    }
    HistoryBucket::Record record;
    static_cast<QuarterHourRow::SpanRow &>(record) = row;
    f.builder.add(record);
    // Complete once the finer record that ends the bucket has arrived.
    if (row.end_epoch >= end && !flush(f)) return false;
    return true;
}

} // namespace

bool bucketBounds(uint32_t weeks, uint32_t epoch, uint32_t &start, uint32_t &end) {
    if (!isLevel(weeks) || epoch < kAnchorEpoch) return false;
    const uint64_t length = static_cast<uint64_t>(weeks) * kWeekSeconds;
    const uint64_t s = kAnchorEpoch + ((epoch - kAnchorEpoch) / length) * length;
    if (s + length > UINT32_MAX) return false;
    start = static_cast<uint32_t>(s);
    end = static_cast<uint32_t>(s + length);
    return true;
}

bool Level::bucketFor(uint32_t quarter_start, Bucket &out) const {
    if (!bucketBounds(1, quarter_start, out.start, out.end)) return false;
    if (!LongHistoryFiles::weeklyFilePath(1, out.path)) return false;
    out.file_start = kAnchorEpoch;
    return true;
}

LadderResult updateLadder(DailyHistoryStorage &storage, LadderStats &stats) {
    stats = LadderStats{};
    for (uint32_t weeks = 1; weeks < kMaxLevelWeeks; weeks *= 2) {
        char finer[LongHistoryFiles::kPathCapacity];
        char coarser[LongHistoryFiles::kPathCapacity];
        if (!LongHistoryFiles::weeklyFilePath(weeks, finer) ||
            !LongHistoryFiles::weeklyFilePath(weeks * 2, coarser)) {
            return LadderResult::Failed;
        }
        bool coarser_exists = false;
        size_t coarser_size = 0;
        if (!storage.fileInfo(coarser, coarser_exists, coarser_size)) return LadderResult::Failed;
        if (!coarser_exists) {
            Counter c;
            LongHistoryFiles::ReadStats rs;
            switch (LongHistoryFiles::readSpanFile(storage, finer, &count, &c, rs)) {
                case LongHistoryFiles::ReadResult::Read:
                case LongHistoryFiles::ReadResult::Absent:
                case LongHistoryFiles::ReadResult::Empty:
                    break;
                default:
                    return LadderResult::Failed;
            }
            if (c.records <= kMaxPointsPerLevel) return LadderResult::Updated;
            LOGI(kTag, "weekly ladder: w%u has %u records; starting w%u", static_cast<unsigned>(weeks),
                 static_cast<unsigned>(c.records), static_cast<unsigned>(weeks * 2));
        }
        Folder f{&storage, coarser, weeks * 2, 0, 0, {}, false, 0, false};
        uint32_t last_end = 0;
        if (SummaryAppender::prepare(storage, coarser, f.last_start, last_end) !=
            SummaryAppender::Result::Ready) {
            return LadderResult::Failed;
        }
        f.after = last_end;
        LongHistoryFiles::ReadStats rs;
        const LongHistoryFiles::ReadResult read = LongHistoryFiles::readSpanFile(storage, finer, &fold, &f, rs);
        if (f.failed || read == LongHistoryFiles::ReadResult::BadHeader ||
            read == LongHistoryFiles::ReadResult::Failed) {
            return LadderResult::Failed;
        }
        stats.records_written += f.written;
        ++stats.levels;
    }
    return LadderResult::Updated;
}

} // namespace WeeklySummary
