// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/SummaryWriter.h"

#include <string.h>

#include "config/AppConfig.h"
#include "core/Logger.h"
#include "modules/ForwardOnlyGuard.h"
#include "modules/SummaryAppender.h"

namespace {
constexpr const char *kTag = "History";
constexpr uint32_t kQuarter = QuarterHourRow::kQuarterSeconds;
} // namespace

SummaryWriter::SummaryWriter(DailyHistoryStorage &storage, const SummaryLevel &level)
    : storage_(storage), level_(level) {}

void SummaryWriter::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    prepared_ = false;
    open_ = false;
    file_path_[0] = '\0';
    closed_start_ = 0;
    status_ = Status{};
    status_.state = enabled ? State::NoCard : State::Off;
}

void SummaryWriter::rowThunk(const QuarterHourRow::Row &row, void *context) {
    static_cast<SummaryWriter *>(context)->onRow(row);
}

void SummaryWriter::fail(DailyStorageFailureKind kind) {
    status_.state = State::WriteError;
    status_.failure_kind = kind;
    prepared_ = false; // repair and rebuild before the next append
    open_ = false;
    file_path_[0] = '\0';
}

bool SummaryWriter::prepareFile(const SummaryLevel::Bucket &bucket, uint32_t &last_end) {
    last_end = 0;
    uint32_t last_start = 0;
    switch (SummaryAppender::prepare(storage_, bucket.path, last_start, last_end)) {
        case SummaryAppender::Result::Ready:
            break;
        case SummaryAppender::Result::BadFile:
            // Damaged beyond a torn tail: the 15-minute rows rebuild it.
            LOGW(kTag, "%s summary %s is not readable; rebuilding it", level_.name(), bucket.path);
            if (!storage_.removeFile(bucket.path)) {
                fail(storage_.lastFailureKind());
                return false;
            }
            ++status_.files_rebuilt;
            last_start = 0;
            last_end = 0;
            break;
        default:
            fail(storage_.lastFailureKind());
            return false;
    }
    strncpy(file_path_, bucket.path, sizeof(file_path_) - 1);
    file_path_[sizeof(file_path_) - 1] = '\0';
    file_last_start_ = last_start;
    return true;
}

bool SummaryWriter::prepare() {
    uint32_t day = 0;
    switch (LongHistoryFiles::findNewestQuarterFile(storage_, day)) {
        case LongHistoryFiles::FindResult::Failed:
            fail(storage_.lastFailureKind());
            return false;
        case LongHistoryFiles::FindResult::None:
            prepared_ = true;
            status_.state = State::Recording;
            return true;
        case LongHistoryFiles::FindResult::Found:
            break;
    }
    char newest_path[LongHistoryFiles::kPathCapacity];
    uint32_t newest = 0;
    if (!LongHistoryFiles::quarterFilePath(day, newest_path) ||
        ForwardOnlyGuard::lastQuarterEpoch(storage_, newest_path, newest) !=
            ForwardOnlyGuard::LastResult::Found) {
        // The 15-minute logger reports an unreadable newest file itself.
        prepared_ = true;
        status_.state = State::Recording;
        return true;
    }
    SummaryLevel::Bucket last_bucket;
    if (!level_.bucketFor(newest, last_bucket)) {
        fail(DailyStorageFailureKind::Invalid);
        return false;
    }
    // Resume after the last record of the newest file, or of the file before
    // it if the newest has none yet (a period boundary during a power cut).
    uint32_t resume = 0;
    if (!prepareFile(last_bucket, resume)) return false;
    if (resume == 0) {
        SummaryLevel::Bucket previous;
        uint32_t previous_end = 0;
        if (last_bucket.file_start > kQuarter &&
            level_.bucketFor(last_bucket.file_start - kQuarter, previous)) {
            // close() prepares the newest file again when it first writes to it.
            if (!prepareFile(previous, previous_end)) return false;
            resume = previous_end ? previous_end : previous.file_start;
        } else {
            resume = last_bucket.file_start;
        }
    }
    prepared_ = true;
    status_.state = State::Recording;
    status_.failure_kind = DailyStorageFailureKind::None;
    open_ = false;
    LongHistoryFiles::ScanStats scan;
    const LongHistoryFiles::ScanResult r =
        LongHistoryFiles::scanQuarterRows(storage_, resume, newest + kQuarter, &rebuildRow, this, scan);
    if (r == LongHistoryFiles::ScanResult::Failed) {
        fail(storage_.lastFailureKind());
        return false;
    }
    if (status_.records_rebuilt > 0) {
        LOGI(kTag, "%s summary: %u records rebuilt from 15-minute rows", level_.name(),
             static_cast<unsigned>(status_.records_rebuilt));
    }
    return prepared_;
}

bool SummaryWriter::rebuildRow(const QuarterHourRow::Row &row, size_t, void *context) {
    SummaryWriter &self = *static_cast<SummaryWriter *>(context);
    self.add(row, true);
    return self.prepared_;
}

void SummaryWriter::poll(time_t now, bool time_trusted, bool ready) {
    if (!enabled_) return;
    if (!storage_.isReady() || !ready) {
        if (!storage_.isReady()) status_.state = State::NoCard;
        prepared_ = false;
        open_ = false;
        file_path_[0] = '\0';
        return;
    }
    if (!prepared_ && !prepare()) return;
    if (open_ && time_trusted && now > Config::TIME_VALID_EPOCH &&
        static_cast<uint64_t>(now) >= static_cast<uint64_t>(bucket_.end) + kCloseDelaySeconds) {
        close(false);
    }
}

void SummaryWriter::onRow(const QuarterHourRow::Row &row) {
    if (!enabled_ || !prepared_) return;
    add(row, false);
}

void SummaryWriter::add(const QuarterHourRow::Row &row, bool rebuilding) {
    SummaryLevel::Bucket b;
    if (!level_.bucketFor(row.start_epoch, b)) {
        LOGW(kTag, "%s summary: no bucket for quarter %u", level_.name(),
             static_cast<unsigned>(row.start_epoch));
        ++status_.refused;
        return;
    }
    if ((open_ && b.start < bucket_.start) || (!open_ && closed_start_ && b.start <= closed_start_)) {
        LOGW(kTag, "%s summary: quarter %u is for a closed bucket; not added", level_.name(),
             static_cast<unsigned>(row.start_epoch));
        ++status_.refused;
        return;
    }
    if (open_ && b.start > bucket_.start) close(rebuilding);
    if (!prepared_) return;
    if (!open_) {
        bucket_ = b;
        builder_.begin(b.start, b.end);
        open_ = true;
    }
    builder_.add(row);
    if (row.start_epoch + kQuarter >= bucket_.end) close(rebuilding);
}

void SummaryWriter::close(bool rebuilding) {
    open_ = false;
    closed_start_ = bucket_.start;
    const HistoryBucket::Record &record = builder_.record();
    if (record.empty()) return;
    if (strcmp(file_path_, bucket_.path) != 0) {
        uint32_t ignored_end = 0;
        if (!prepareFile(bucket_, ignored_end)) return;
    }
    switch (SummaryAppender::append(storage_, file_path_, record, file_last_start_)) {
        case SummaryAppender::Result::Appended:
            if (rebuilding) {
                ++status_.records_rebuilt;
            } else {
                ++status_.records_written;
            }
            status_.state = State::Recording;
            return;
        case SummaryAppender::Result::ClockBehind:
            ++status_.refused;
            return;
        case SummaryAppender::Result::BadRecord:
            fail(DailyStorageFailureKind::Invalid);
            return;
        default:
            fail(storage_.lastFailureKind());
            return;
    }
}
