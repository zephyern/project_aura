// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "modules/DailyHistoryStorage.h"

// The one storage-health verdict of the long-term history (pure), shown in
// the alert bar (docs/LONG_HISTORY.md, "Storage health").
namespace StorageHealth {

// Below this many days of space left at the measured rate, LowSpace.
constexpr uint32_t kLowSpaceDays = 30;
// Days measured for the rate (the most recent complete UTC days).
constexpr size_t kRateDays = 7;

enum class FullAction : uint8_t { AutoDelete, StopRecording };

enum class Kind : uint8_t {
    Off,              // log off: nothing to show
    Ok,
    NoCard,           // log on, card missing or not mounted
    WriteError,       // last write failed (failure_kind)
    RecordingStopped, // card full and nothing left to delete, or full with StopRecording
    ClockBehind,      // a row was refused because the clock is behind the files
    Full,             // card full; AutoDelete is deleting the oldest detail
    LowSpace,         // fewer than kLowSpaceDays days left (days_left)
};

struct Inputs {
    bool log_enabled = false;
    bool card_ready = false;
    bool write_error = false;
    DailyStorageFailureKind failure_kind = DailyStorageFailureKind::None;
    bool clock_behind = false;
    uint64_t free_bytes = 0;
    uint32_t bytes_per_day = 0; // 0: not measured yet (no complete day)
    FullAction full_action = FullAction::AutoDelete;
    bool nothing_left_to_delete = false; // AutoDelete found no whole month of 15-minute files
};

struct Status {
    Kind kind = Kind::Off;
    uint32_t days_left = 0;                                           // LowSpace only
    DailyStorageFailureKind failure_kind = DailyStorageFailureKind::None; // WriteError only
};

// The card counts as full when less than one day's data fits, or when a
// write failed for lack of space.
Status evaluate(const Inputs &in);

// Days of data that fit in `free_bytes` at `bytes_per_day` (rounded down).
uint32_t daysLeft(uint64_t free_bytes, uint32_t bytes_per_day);

// Bytes the long-term history writes per day, measured on the card: the mean
// size of the given complete days' 15-minute files, plus the hourly and daily
// records those days produce at the measured size of each kind of record.
struct RateSample {
    const uint32_t *quarter_file_bytes = nullptr; // one per complete UTC day
    size_t days = 0;
    uint32_t hourly_record_bytes = 0; // mean length of an hourly record (0: none yet)
    uint32_t daily_record_bytes = 0;  // mean length of a daily record (0: none yet)
};
uint32_t bytesPerDay(const RateSample &sample);

} // namespace StorageHealth
