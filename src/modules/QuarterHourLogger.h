// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>
#include <time.h>

#include "modules/ChartsHistory.h"
#include "modules/DailyHistoryStorage.h"
#include "modules/QuarterHourRow.h"

// Long-term history writer (docs/LONG_HISTORY.md): listens to ChartsHistory's
// completed five-minute intervals, combines the three of each UTC quarter hour
// exactly, and appends one row per quarter to the UTC day file
// /aura/history/15min/YYYY/MM/YYYY-MM-DD.csv. Only active while enabled
// (setting history.log_enabled, off by default).
class QuarterHourLogger {
public:
    enum class State : uint8_t {
        Off,         // setting off: nothing is read or written
        NoCard,      // enabled, but the SD card is not ready
        Recording,   // the last write (or preparation) succeeded
        ClockBehind, // a quarter was refused: it does not start after the last row
        WriteError,  // the last preparation or write failed; see failure_kind
    };
    struct Status {
        State state = State::Off;
        uint32_t last_epoch = 0; // start of the newest row on the card (0 = none)
        DailyStorageFailureKind failure_kind = DailyStorageFailureKind::None;
        uint32_t rows_written = 0;
        uint32_t quarters_refused = 0;
    };

    // Seconds after a quarter ends before poll() writes it without its last
    // interval (ChartsHistory completes an interval when the next one starts).
    static constexpr uint32_t kCloseGraceSeconds = 300;

    explicit QuarterHourLogger(DailyHistoryStorage &storage);

    // Turning on prepares the card on the next poll(); turning off drops the
    // unwritten quarter (at most 15 minutes, as for a power cut).
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    // Registers with ChartsHistory as its interval listener.
    bool attach(ChartsHistory &history);

    // Call each loop pass after ChartsHistory::update. Prepares the card once
    // it is ready (torn-tail repair, then the last row's time) and writes a
    // quarter whose grace period has passed. `now` is used only if trusted.
    void poll(time_t now, bool time_trusted);

    void onInterval(const ChartsHistory::CompletedInterval &interval);

    Status status() const { return status_; }
    // True once the card is prepared (repair done, last row known).
    bool prepared() const { return enabled_ && prepared_; }

    // Each summary level (hourly, daily, weekly) receives every row after it
    // is written to the card. False if the pair is already registered or all
    // slots are taken.
    static constexpr size_t kMaxRowListeners = 3;
    using RowListener = void (*)(const QuarterHourRow::Row &row, void *context);
    bool addRowListener(RowListener listener, void *context);

private:
    static void intervalThunk(const ChartsHistory::CompletedInterval &interval, void *context);
    bool prepare();
    void openQuarter(uint32_t start);
    void closeQuarter();
    void fail(DailyStorageFailureKind kind);

    DailyHistoryStorage &storage_;
    bool enabled_ = false;
    bool prepared_ = false;
    bool open_ = false;
    uint32_t closed_start_ = 0; // start of the last quarter closed this session
    QuarterHourRow::Row quarter_{};
    Status status_{};
    struct RowListenerSlot {
        RowListener fn = nullptr;
        void *context = nullptr;
    };
    RowListenerSlot row_listeners_[kMaxRowListeners]{};
};
