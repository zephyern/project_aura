// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>
#include <time.h>

#include "modules/DailyHistoryStorage.h"
#include "modules/HistoryBucket.h"
#include "modules/LongHistoryFiles.h"
#include "modules/QuarterHourRow.h"

// How one summary level groups 15-minute rows (docs/LONG_HISTORY.md).
class SummaryLevel {
public:
    struct Bucket {
        uint32_t start = 0;
        uint32_t end = 0; // exclusive
        char path[LongHistoryFiles::kPathCapacity] = {};
        uint32_t file_start = 0; // start of the period the file covers
    };
    virtual ~SummaryLevel() = default;
    virtual const char *name() const = 0;
    // The bucket containing the quarter starting at `quarter_start`, and its file.
    virtual bool bucketFor(uint32_t quarter_start, Bucket &out) const = 0;
};

// Writes one summary level from the 15-minute rows: live as rows are written,
// and at preparation by rebuilding what is missing from the 15-minute files.
class SummaryWriter {
public:
    enum class State : uint8_t { Off, NoCard, Recording, WriteError };
    struct Status {
        State state = State::Off;
        DailyStorageFailureKind failure_kind = DailyStorageFailureKind::None;
        uint32_t records_written = 0;
        uint32_t records_rebuilt = 0;
        uint32_t files_rebuilt = 0; // damaged files removed and rebuilt
        uint32_t refused = 0;       // late rows, clock behind
    };
    // Seconds after a bucket ends before poll() writes it without a final row
    // (the 15-minute logger writes the last quarter up to 5 minutes after it ends).
    static constexpr uint32_t kCloseDelaySeconds = 600;

    SummaryWriter(DailyHistoryStorage &storage, const SummaryLevel &level);

    void setEnabled(bool enabled);
    // Call each loop pass after the 15-minute logger's poll. `ready` is true
    // once the 15-minute logger has prepared the card.
    void poll(time_t now, bool time_trusted, bool ready);
    void onRow(const QuarterHourRow::Row &row);
    static void rowThunk(const QuarterHourRow::Row &row, void *context);

    Status status() const { return status_; }
    // The bucket in progress (empty when none).
    const HistoryBucket::Record &openRecord() const { return builder_.record(); }
    bool hasOpenBucket() const { return open_; }

private:
    bool prepare();
    bool prepareFile(const SummaryLevel::Bucket &bucket, uint32_t &last_end);
    void add(const QuarterHourRow::Row &row, bool rebuilding);
    void close(bool rebuilding);
    void fail(DailyStorageFailureKind kind);
    static bool rebuildRow(const QuarterHourRow::Row &row, size_t, void *context);

    DailyHistoryStorage &storage_;
    const SummaryLevel &level_;
    bool enabled_ = false;
    bool prepared_ = false;
    bool open_ = false;
    SummaryLevel::Bucket bucket_{};
    HistoryBucket::Builder builder_;
    char file_path_[LongHistoryFiles::kPathCapacity] = {};
    uint32_t file_last_start_ = 0;
    uint32_t floor_ = 0; // end of the last bucket closed (written or empty)
    Status status_{};
};
