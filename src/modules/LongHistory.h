// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <atomic>
#include <vector>

#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include "modules/ChartsHistory.h"
#include "modules/DailyHistoryStorage.h"
#include "modules/DailySummary.h"
#include "modules/HourlySummary.h"
#include "modules/QuarterHourLogger.h"
#include "modules/StorageHealth.h"
#include "modules/SummaryWriter.h"
#include "modules/WeeklySummary.h"

// The long-term history as one unit (docs/LONG_HISTORY.md): the 15-minute
// logger, the hourly, daily and weekly writers and the week ladder, plus the
// web operations on them (daily CSV download, today's CSV, clear all, clear
// today). The main loop calls attach/setEnabled/poll; the web task calls the
// rest. When the history log is on this replaces the upstream daily history.
class LongHistory {
public:
    explicit LongHistory(DailyHistoryStorage &storage);
    ~LongHistory();
    LongHistory(const LongHistory &) = delete;
    LongHistory &operator=(const LongHistory &) = delete;

    // False (logged) if the charts interval listener or a row listener is taken.
    bool attach(ChartsHistory &history);
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    // What to do when the card is full (setting history.full_action).
    void setFullAction(StorageHealth::FullAction action) { full_action_.store(action); }
    // Free space on the card as last read by the SD card manager.
    struct Space {
        bool known = false;
        uint64_t free_bytes = 0;
    };
    // Call once per loop pass after ChartsHistory::update. Also applies the
    // card-full action and computes the storage health.
    void poll(time_t now, bool time_trusted, const Space &space);
    // The verdict of the last poll (any task; never blocks).
    StorageHealth::Status health() const;

    // The daily CSV in the upstream daily.csv format, generated from the
    // daily records. Chunks go to `sink`; `bytes` is the total. Only records
    // ending at or before `end_limit` are included and `last_end` returns
    // the newest end included, so a second call with that limit produces
    // exactly the same text (records are only ever appended).
    using TextSink = bool (*)(const char *text, size_t len, void *context);
    enum class CsvResult : uint8_t { Ready, NoRecords, Failed };
    CsvResult dailyCsv(bool units_c, uint32_t end_limit, TextSink sink, void *context, size_t &bytes,
                       uint32_t &last_end) const;
    // Today's record so far (the daily writer's open bucket), same format with header.
    bool currentDayCsv(bool units_c, String &out) const;

    // Earliest local day ("YYYY-MM-DD") of any record; false when there is none.
    bool firstDay(char *out, size_t len) const;
    // The same as a day key (YYYYMMDD).
    bool firstDayKey(uint32_t &day_key) const;
    // Readers for the device's long chart ranges (docs/CHARTS_HISTORY.md),
    // called from the main loop. Each record goes to `fn` in time order,
    // followed by the summary still in progress when it belongs to the range.
    // Busy: a web operation holds the history; try again later.
    enum class ChartRead : uint8_t { Read, Off, Busy, Failed };
    using ChartRecordFn = void (*)(const QuarterHourRow::SpanRow &row, void *context);
    // Hourly records starting in [start, end).
    ChartRead hourlyChart(uint32_t start, uint32_t end, ChartRecordFn fn, void *context) const;
    // Local day key and largest sample count of today's record so far.
    bool currentDay(uint32_t &day_key, uint32_t &sample_count) const;
    bool lastWriteOk() const;

    // One entry per removed group or cut file.
    struct OpResult {
        String path;
        bool existed = false;
        bool removed = false;
        bool ok = false;
    };
    struct OpResults {
        std::vector<OpResult> items;
        bool ok() const;
        void add(const char *path, bool existed, bool removed, bool ok);
    };
    // Busy: another operation or the main loop holds the history. NoTime:
    // today's local midnight could not be computed (clock not set).
    enum class OpStatus : uint8_t { Done, Busy, Off, NoTime };
    // Deletes every long-term history file and the upstream daily files, then
    // starts recording again from nothing.
    OpStatus clearAll(OpResults &results);
    // Deletes everything from today's local midnight on (15-minute rows and
    // the summaries that contain them) and rebuilds the summaries from the
    // 15-minute rows that remain. Earlier days are untouched. Only files that
    // existed (or failed) are listed.
    OpStatus clearToday(time_t now, OpResults &results);
    // "Clear future-dated history" (clock was once set ahead): removes every
    // 15-minute row that starts after `now` and every summary record that
    // ends after it, then rebuilds. NoTime unless the clock was trusted at
    // the last poll (a wrong clock must never delete real history).
    OpStatus clearFuture(time_t now, OpResults &results);

private:
    class Lock {
    public:
        Lock(const LongHistory &owner, uint32_t timeout_ms);
        ~Lock();
        bool locked() const { return locked_; }

    private:
        const LongHistory &owner_;
        bool locked_;
    };
    static void intervalThunk(const ChartsHistory::CompletedInterval &interval, void *context);
    void setWritersEnabled(bool enabled);
    void updateLadder();
    enum class DeleteResult : uint8_t { Deleted, NothingLeft, Failed };
    // Card full, auto_delete: removes the oldest whole month of 15-minute
    // files, never the current UTC month and never a summary.
    DeleteResult deleteOldestMonth(time_t now);
    void measureRate(time_t now);
    StorageHealth::Inputs healthInputs(const Space &space) const;
    void enforceSpace(time_t now, const Space &space);
    void publishHealth(const StorageHealth::Status &status);
    OpStatus beginOp();
    void endOp();

    DailyHistoryStorage &storage_;
    QuarterHourLogger logger_;
    const HourlySummary::Level hourly_level_;
    const DailySummary::Level daily_level_;
    const WeeklySummary::Level weekly_level_;
    SummaryWriter hourly_;
    SummaryWriter daily_;
    SummaryWriter weekly_;
    uint32_t weekly_records_seen_ = UINT32_MAX; // forces one ladder update after preparation
    bool attached_ = false;
    bool enabled_ = false;
    bool op_running_ = false;
    std::atomic<StorageHealth::FullAction> full_action_{StorageHealth::kDefaultFullAction};
    std::atomic<bool> time_trusted_{false}; // as given to the last poll
    uint32_t rate_day_ = 0;       // UTC day key the rate was last measured on
    uint32_t bytes_per_day_ = 0;  // 0: not measured (no complete day yet)
    bool nothing_left_ = false;   // auto_delete found nothing it may delete
    std::atomic<bool> stopped_{false}; // recording stopped: intervals are dropped
    uint32_t rows_at_delete_ = UINT32_MAX; // rows written at the last deletion
    uint64_t free_at_delete_ = UINT64_MAX; // free-space reading at the last deletion
    DailyStorageFailureKind delete_failure_ = DailyStorageFailureKind::None; // last deletion failed
    std::atomic<uint64_t> health_{0};
#ifndef UNIT_TEST
    mutable SemaphoreHandle_t mutex_ = nullptr;
#endif
};
