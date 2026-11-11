// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/LongHistory.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "core/LocalDate.h"
#include "core/Logger.h"
#include "modules/DailyExtremaHistory.h"
#include "modules/ForwardOnlyGuard.h"
#include "modules/LongHistoryFiles.h"

namespace {

constexpr const char *kTag = "History";
constexpr uint32_t kWebLockTimeoutMs = 5000;
constexpr uint32_t kIntervalLockTimeoutMs = 1000;
constexpr uint32_t kDaySeconds = 86400;
constexpr uint32_t kQuartersPerDay = kDaySeconds / QuarterHourRow::kQuarterSeconds;
// A summary record is a 15-minute row plus ",<end_utc>" (10 digits).
constexpr uint32_t kSpanExtraBytes = 11;

uint64_t packHealth(const StorageHealth::Status &s) {
    return static_cast<uint64_t>(s.kind) | (static_cast<uint64_t>(s.failure_kind) << 8) |
           (static_cast<uint64_t>(s.days_left) << 32);
}

StorageHealth::Status unpackHealth(uint64_t v) {
    StorageHealth::Status s;
    s.kind = static_cast<StorageHealth::Kind>(v & 0xFFU);
    s.failure_kind = static_cast<DailyStorageFailureKind>((v >> 8) & 0xFFU);
    s.days_left = static_cast<uint32_t>(v >> 32);
    return s;
}

// The local day a daily record belongs to: its middle. A record is one local
// day (23-25 hours) in the zone in effect when it was written, so its middle
// stays inside that day even if the zone has changed since.
bool recordDayKey(const QuarterHourRow::SpanRow &row, uint32_t &day_key) {
    const uint32_t middle = row.start_epoch + (row.end_epoch - row.start_epoch) / 2;
    return LocalDate::localDayKey(static_cast<time_t>(middle), day_key);
}

void appendRecordCsv(const QuarterHourRow::SpanRow &row, bool units_c, String &out) {
    uint32_t day_key = 0;
    if (!recordDayKey(row, day_key)) return;
    char day[16] = {};
    LocalDate::formatDayKey(day_key, day, sizeof(day));
    for (uint8_t i = 0; i < ChartsHistory::kMetricCount; ++i) {
        const HistoryAggregate &a = row.metrics[i];
        if (a.empty()) continue;
        DailyExtremaHistory::CsvMetricValues v;
        v.min_value = a.min;
        v.min_epoch = a.min_epoch;
        v.max_value = a.max;
        v.max_epoch = a.max_epoch;
        v.sample_count = a.count;
        const uint8_t gas_type = i == ChartsHistory::METRIC_OPTIONAL_GAS ? a.source : 0;
        DailyExtremaHistory::appendCsvMetricRow(out, day, i, v, units_c, gas_type);
    }
}

// Local years that have a daily file, oldest first.
bool collectYear(const char *name, bool is_dir, void *context) {
    if (is_dir) return true;
    unsigned year = 0;
    char tail[8] = {};
    if (sscanf(name, "%4u%7s", &year, tail) == 2 && strcmp(tail, ".csv") == 0) {
        static_cast<std::vector<uint32_t> *>(context)->push_back(year);
    }
    return true;
}
bool dailyYears(const DailyHistoryStorage &storage, std::vector<uint32_t> &years) {
    years.clear();
    switch (storage.listDir(LongHistoryFiles::kDailyRoot, &collectYear, &years)) {
        case DirListResult::Listed:
            std::sort(years.begin(), years.end());
            return true;
        case DirListResult::Absent:
            return true;
        case DirListResult::Failed:
            return false;
    }
    return false;
}

struct CsvPass {
    bool units_c;
    uint32_t end_limit;
    LongHistory::TextSink sink;
    void *context;
    size_t bytes = 0;
    uint32_t last_end = 0;
    bool any = false;
    bool stopped = false; // reached a record after end_limit
    bool sink_failed = false;
};
bool emit(CsvPass &p, const char *text, size_t len) {
    p.bytes += len;
    if (p.sink && !p.sink(text, len, p.context)) {
        p.sink_failed = true;
        return false;
    }
    return true;
}
bool csvRecord(const QuarterHourRow::SpanRow &row, size_t, void *context) {
    CsvPass &p = *static_cast<CsvPass *>(context);
    if (row.end_epoch > p.end_limit) {
        p.stopped = true;
        return false;
    }
    String text;
    if (!p.any) text += DailyExtremaHistory::csvHeader();
    appendRecordCsv(row, p.units_c, text);
    p.any = true;
    p.last_end = row.end_epoch;
    return emit(p, text.c_str(), text.length());
}

struct First {
    bool found = false;
    QuarterHourRow::SpanRow row;
};
bool firstRecord(const QuarterHourRow::SpanRow &row, size_t, void *context) {
    First &f = *static_cast<First *>(context);
    f.found = true;
    f.row = row;
    return false;
}

uint32_t largestCount(const QuarterHourRow::SpanRow &row) {
    uint32_t n = 0;
    for (const HistoryAggregate &a : row.metrics) n = std::max(n, a.count);
    return n;
}

const char *treeGroups[] = {
    LongHistoryFiles::kQuarterRoot,
    LongHistoryFiles::kHourlyRoot,
    LongHistoryFiles::kDailyRoot,
    LongHistoryFiles::kWeeklyRoot,
};

} // namespace

LongHistory::Lock::Lock(const LongHistory &owner, uint32_t timeout_ms) : owner_(owner), locked_(false) {
#ifdef UNIT_TEST
    (void)timeout_ms;
    locked_ = true;
#else
    locked_ = owner_.mutex_ && xSemaphoreTake(owner_.mutex_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
#endif
}

LongHistory::Lock::~Lock() {
#ifndef UNIT_TEST
    if (locked_) xSemaphoreGive(owner_.mutex_);
#endif
}

LongHistory::LongHistory(DailyHistoryStorage &storage)
    : storage_(storage),
      logger_(storage),
      hourly_(storage, hourly_level_),
      daily_(storage, daily_level_),
      weekly_(storage, weekly_level_) {
#ifndef UNIT_TEST
    mutex_ = xSemaphoreCreateMutex();
#endif
}

LongHistory::~LongHistory() {
#ifndef UNIT_TEST
    if (mutex_) vSemaphoreDelete(mutex_);
#endif
}

bool LongHistory::attach(ChartsHistory &history) {
#ifndef UNIT_TEST
    if (!mutex_) {
        LOGE(kTag, "long-term history lock could not be created; long-term history disabled");
        return false;
    }
#endif
    if (!history.setIntervalListener(&LongHistory::intervalThunk, this)) {
        LOGE(kTag, "charts interval listener already taken; long-term history disabled");
        return false;
    }
    SummaryWriter *writers[] = {&hourly_, &daily_, &weekly_};
    for (SummaryWriter *w : writers) {
        if (!logger_.addRowListener(&SummaryWriter::rowThunk, w)) {
            LOGE(kTag, "a summary could not listen to 15-minute rows; long-term history disabled");
            return false;
        }
    }
    attached_ = true;
    return true;
}

void LongHistory::setWritersEnabled(bool enabled) {
    logger_.setEnabled(enabled);
    hourly_.setEnabled(enabled);
    daily_.setEnabled(enabled);
    weekly_.setEnabled(enabled);
    weekly_records_seen_ = UINT32_MAX;
}

void LongHistory::setEnabled(bool enabled) {
    Lock lock(*this, kWebLockTimeoutMs);
    if (!lock.locked()) {
        LOGE(kTag, "long-term history busy; setting not applied");
        return;
    }
    enabled_ = enabled && attached_;
    if (!op_running_) setWritersEnabled(enabled_);
}

void LongHistory::intervalThunk(const ChartsHistory::CompletedInterval &interval, void *context) {
    LongHistory &self = *static_cast<LongHistory *>(context);
    if (self.stopped_.load()) return; // card full, recording stopped (alert bar says so)
    Lock lock(self, kIntervalLockTimeoutMs);
    if (!lock.locked()) {
        LOGE(kTag, "long-term history busy; five-minute interval %u not recorded",
             static_cast<unsigned>(interval.entry.start_epoch));
        return;
    }
    self.logger_.onInterval(interval);
}

void LongHistory::poll(time_t now, bool time_trusted, const Space &space) {
    time_trusted_.store(time_trusted);
    Lock lock(*this, 0); // a web operation holds it only briefly; try again next pass
    if (!lock.locked() || op_running_) return;
    if (!enabled_) {
        publishHealth(StorageHealth::Status{});
        return;
    }
    logger_.poll(now, time_trusted);
    const bool ready = logger_.prepared();
    hourly_.poll(now, time_trusted, ready);
    daily_.poll(now, time_trusted, ready);
    weekly_.poll(now, time_trusted, ready);
    updateLadder();
    if (time_trusted) measureRate(now);
    enforceSpace(now, space);
}

void LongHistory::updateLadder() {
    const SummaryWriter::Status weekly = weekly_.status();
    if (weekly.state != SummaryWriter::State::Recording) return;
    const uint32_t seen = weekly.records_written + weekly.records_rebuilt;
    if (seen == weekly_records_seen_) return;
    weekly_records_seen_ = seen;
    WeeklySummary::LadderStats ladder;
    if (WeeklySummary::updateLadder(storage_, ladder) != WeeklySummary::LadderResult::Updated) {
        LOGE(kTag, "weekly ladder update failed (%u)", static_cast<unsigned>(storage_.lastFailureKind()));
    }
}

void LongHistory::measureRate(time_t now) {
    uint32_t today = 0;
    if (!LocalDate::utcDayKey(now, today) || today == rate_day_) return;
    rate_day_ = today;
    uint32_t sizes[StorageHealth::kRateDays] = {};
    size_t days = 0;
    for (size_t back = 1; back <= StorageHealth::kRateDays; ++back) {
        uint32_t key = 0;
        char path[LongHistoryFiles::kPathCapacity];
        const time_t day = now - static_cast<time_t>(back * kDaySeconds);
        if (!LocalDate::utcDayKey(day, key) || !LongHistoryFiles::quarterFilePath(key, path)) continue;
        bool exists = false;
        size_t size = 0;
        if (!storage_.fileInfo(path, exists, size) || !exists || size == 0) continue;
        sizes[days++] = static_cast<uint32_t>(size);
    }
    StorageHealth::RateSample sample;
    sample.quarter_file_bytes = sizes;
    sample.days = days;
    if (days != 0) {
        uint64_t total = 0;
        for (size_t i = 0; i < days; ++i) total += sizes[i];
        const uint32_t row_bytes = static_cast<uint32_t>(total / days / kQuartersPerDay);
        sample.hourly_record_bytes = row_bytes + kSpanExtraBytes;
        sample.daily_record_bytes = row_bytes + kSpanExtraBytes;
    }
    bytes_per_day_ = StorageHealth::bytesPerDay(sample);
}

StorageHealth::Inputs LongHistory::healthInputs(const Space &space) const {
    StorageHealth::Inputs in;
    in.log_enabled = true;
    const QuarterHourLogger::Status q = logger_.status();
    in.card_ready = storage_.isReady() && q.state != QuarterHourLogger::State::NoCard;
    if (q.state == QuarterHourLogger::State::WriteError) {
        in.write_error = true;
        in.failure_kind = q.failure_kind;
    }
    const SummaryWriter *writers[] = {&hourly_, &daily_, &weekly_};
    for (const SummaryWriter *w : writers) {
        const SummaryWriter::Status s = w->status();
        if (!in.write_error && s.state == SummaryWriter::State::WriteError) {
            in.write_error = true;
            in.failure_kind = s.failure_kind;
        }
    }
    // Recording stopped on a "no space" error: nothing is written while
    // stopped, so the error cannot clear itself; trust the space reading.
    if (stopped_.load() && in.write_error && in.failure_kind == DailyStorageFailureKind::NoSpace &&
        space.known && bytes_per_day_ != 0 && space.free_bytes >= bytes_per_day_) {
        in.write_error = false;
        in.failure_kind = DailyStorageFailureKind::None;
    }
    in.clock_behind = q.state == QuarterHourLogger::State::ClockBehind;
    in.free_bytes = space.free_bytes;
    in.bytes_per_day = space.known ? bytes_per_day_ : 0;
    in.full_action = full_action_.load();
    in.nothing_left_to_delete = nothing_left_;
    return in;
}

LongHistory::DeleteResult LongHistory::deleteOldestMonth(time_t now) {
    uint32_t year = 0, month = 0, today = 0;
    switch (LongHistoryFiles::findOldestQuarterMonth(storage_, year, month)) {
        case LongHistoryFiles::FindResult::Failed:
            return DeleteResult::Failed;
        case LongHistoryFiles::FindResult::None:
            return DeleteResult::NothingLeft;
        case LongHistoryFiles::FindResult::Found:
            break;
    }
    if (!LocalDate::utcDayKey(now, today)) return DeleteResult::Failed;
    if (year == today / 10000U && month == (today / 100U) % 100U) return DeleteResult::NothingLeft;
    char path[LongHistoryFiles::kPathCapacity];
    if (!LongHistoryFiles::quarterMonthPath(year, month, path)) return DeleteResult::Failed;
    if (LongHistoryFiles::removeTree(storage_, path) == LongHistoryFiles::TreeRemoveResult::Failed) {
        return DeleteResult::Failed;
    }
    char year_path[LongHistoryFiles::kPathCapacity];
    snprintf(year_path, sizeof(year_path), "%s/%04u", LongHistoryFiles::kQuarterRoot, static_cast<unsigned>(year));
    if (storage_.removeEmptyDir(year_path) == DirRemoveResult::Failed) return DeleteResult::Failed;
    LOGW(kTag, "SD card full: deleted the 15-minute detail of %04u-%02u (summaries kept)",
         static_cast<unsigned>(year), static_cast<unsigned>(month));
    return DeleteResult::Deleted;
}

void LongHistory::enforceSpace(time_t now, const Space &space) {
    StorageHealth::Inputs in = healthInputs(space);
    StorageHealth::Status status = StorageHealth::evaluate(in);
    const uint32_t rows = logger_.status().rows_written;
    if (status.kind == StorageHealth::Kind::Full && full_action_.load() == StorageHealth::FullAction::AutoDelete &&
        (rows != rows_at_delete_ || space.free_bytes != free_at_delete_)) {
        // After a deletion, wait for a new free-space reading (or a written
        // row) so the card manager catches up before anything more goes.
        rows_at_delete_ = rows;
        free_at_delete_ = space.free_bytes;
        switch (deleteOldestMonth(now)) {
            case DeleteResult::Deleted:
                delete_failure_ = DailyStorageFailureKind::None;
                break;
            case DeleteResult::NothingLeft:
                nothing_left_ = true;
                in.nothing_left_to_delete = true;
                status = StorageHealth::evaluate(in);
                break;
            case DeleteResult::Failed:
                delete_failure_ = storage_.lastFailureKind();
                if (delete_failure_ == DailyStorageFailureKind::None) delete_failure_ = DailyStorageFailureKind::Unknown;
                LOGE(kTag, "SD card full: could not delete old 15-minute detail (%u)",
                     static_cast<unsigned>(delete_failure_));
                break;
        }
    }
    if (status.kind != StorageHealth::Kind::Full) delete_failure_ = DailyStorageFailureKind::None;
    if (delete_failure_ != DailyStorageFailureKind::None) {
        // Full, but the oldest detail could not be deleted: say so, never
        // "deleting oldest detail".
        status.kind = StorageHealth::Kind::WriteError;
        status.failure_kind = delete_failure_;
    }
    if (status.kind != StorageHealth::Kind::Full && status.kind != StorageHealth::Kind::RecordingStopped) {
        nothing_left_ = false; // space again
    }
    const bool stop = status.kind == StorageHealth::Kind::RecordingStopped;
    if (stop != stopped_.load()) {
        if (stop) {
            LOGE(kTag, "SD card full: long-term recording stopped");
        } else {
            LOGI(kTag, "SD card has space again: long-term recording resumed");
        }
        stopped_.store(stop);
    }
    publishHealth(status);
}

void LongHistory::publishHealth(const StorageHealth::Status &status) { health_.store(packHealth(status)); }

StorageHealth::Status LongHistory::health() const { return unpackHealth(health_.load()); }

LongHistory::CsvResult LongHistory::dailyCsv(bool units_c, uint32_t end_limit, TextSink sink, void *context,
                                             size_t &bytes, uint32_t &last_end) const {
    bytes = 0;
    last_end = 0;
    std::vector<uint32_t> years;
    if (!dailyYears(storage_, years)) return CsvResult::Failed;
    CsvPass pass{units_c, end_limit, sink, context};
    for (uint32_t year : years) {
        DailySummary::ReadStats stats;
        if (DailySummary::readYear(storage_, year, &csvRecord, &pass, stats) != DailySummary::ReadResult::Read ||
            pass.sink_failed) {
            return CsvResult::Failed;
        }
        if (pass.stopped) break; // later years only hold later records
    }
    bytes = pass.bytes;
    last_end = pass.last_end;
    return pass.any ? CsvResult::Ready : CsvResult::NoRecords;
}

bool LongHistory::currentDayCsv(bool units_c, String &out) const {
    HistoryBucket::Record record;
    {
        Lock lock(*this, kWebLockTimeoutMs);
        if (!lock.locked() || !daily_.hasOpenBucket()) return false;
        record = daily_.openRecord();
    }
    if (record.empty()) return false;
    out = DailyExtremaHistory::csvHeader();
    appendRecordCsv(record, units_c, out);
    return true;
}

bool LongHistory::currentDay(uint32_t &day_key, uint32_t &sample_count) const {
    Lock lock(*this, kWebLockTimeoutMs);
    if (!lock.locked() || !daily_.hasOpenBucket()) return false;
    const HistoryBucket::Record &record = daily_.openRecord();
    if (!recordDayKey(record, day_key)) return false;
    sample_count = largestCount(record);
    return true;
}

bool LongHistory::lastWriteOk() const {
    Lock lock(*this, kWebLockTimeoutMs);
    if (!lock.locked()) return false;
    const QuarterHourLogger::State q = logger_.status().state;
    return q == QuarterHourLogger::State::Recording && daily_.status().state == SummaryWriter::State::Recording;
}

bool LongHistory::firstDailyRecord(QuarterHourRow::SpanRow &row) const {
    std::vector<uint32_t> years;
    First first;
    if (dailyYears(storage_, years)) {
        for (uint32_t year : years) {
            DailySummary::ReadStats stats;
            if (DailySummary::readYear(storage_, year, &firstRecord, &first, stats) !=
                DailySummary::ReadResult::Read) {
                return false;
            }
            if (first.found) break;
        }
    }
    if (!first.found) {
        Lock lock(*this, kWebLockTimeoutMs);
        if (!lock.locked() || !daily_.hasOpenBucket() || daily_.openRecord().empty()) return false;
        first.row = daily_.openRecord();
    }
    row = first.row;
    return true;
}

bool LongHistory::firstDayKey(uint32_t &day_key) const {
    QuarterHourRow::SpanRow row;
    return firstDailyRecord(row) && recordDayKey(row, day_key);
}

bool LongHistory::firstRecordStart(uint32_t &start) const {
    QuarterHourRow::SpanRow row;
    if (!firstDailyRecord(row)) return false;
    start = row.start_epoch;
    return true;
}

bool LongHistory::firstDay(char *out, size_t len) const {
    uint32_t day_key = 0;
    if (!firstDayKey(day_key)) return false;
    LocalDate::formatDayKey(day_key, out, len);
    return true;
}

namespace {

constexpr uint32_t kDayKeyYearDivisor = 10000U; // YYYYMMDD -> YYYY

struct ChartPass {
    LongHistory::ChartRecordFn fn;
    void *context;
    uint32_t start;
    uint32_t end;
    uint32_t last_end = 0; // end of the last record delivered
};
bool inChartRange(const ChartPass &pass, const QuarterHourRow::SpanRow &row) {
    return row.start_epoch >= pass.start && row.start_epoch < pass.end;
}
bool chartRecord(const QuarterHourRow::SpanRow &row, size_t, void *context) {
    ChartPass &pass = *static_cast<ChartPass *>(context);
    if (inChartRange(pass, row)) {
        pass.fn(row, pass.context);
        pass.last_end = std::max(pass.last_end, row.end_epoch);
    }
    return true;
}

} // namespace

LongHistory::ChartRead LongHistory::hourlyChart(uint32_t start, uint32_t end, ChartRecordFn fn,
                                                void *context) const {
    return chartRead(ChartLevel::Hourly, 0, start, end, fn, context);
}

LongHistory::ChartRead LongHistory::dailyChart(uint32_t start, uint32_t end, ChartRecordFn fn,
                                               void *context) const {
    return chartRead(ChartLevel::Daily, 0, start, end, fn, context);
}

LongHistory::ChartRead LongHistory::weeklyChart(uint32_t weeks, uint32_t start, uint32_t end, ChartRecordFn fn,
                                                void *context) const {
    return chartRead(ChartLevel::Weekly, weeks, start, end, fn, context);
}

LongHistory::ChartRead LongHistory::chartRead(ChartLevel level, uint32_t weeks, uint32_t start, uint32_t end,
                                              ChartRecordFn fn, void *context) const {
    if (!enabled_) return ChartRead::Off;
    if (!fn || end <= start) {
        LOGE(kTag, "chart read with no callback or an empty range");
        return ChartRead::Failed;
    }
    Lock lock(*this, 0); // the main loop's own pass; a web operation holds it only briefly
    if (!lock.locked() || op_running_) return ChartRead::Busy;
    ChartPass pass{fn, context, start, end};
    const SummaryWriter *open_writer = &hourly_;
    if (level == ChartLevel::Hourly) {
        HourlySummary::ReadStats stats;
        if (HourlySummary::readRange(storage_, start, end, &chartRecord, &pass, stats) !=
            HourlySummary::ReadResult::Read) {
            LOGE(kTag, "hourly chart read failed (%u bad lines)", static_cast<unsigned>(stats.skipped));
            return ChartRead::Failed;
        }
    } else if (level == ChartLevel::Weekly) {
        char path[LongHistoryFiles::kPathCapacity];
        char week_path[LongHistoryFiles::kPathCapacity];
        if (!LongHistoryFiles::weeklyFilePath(weeks, path) || !LongHistoryFiles::weeklyFilePath(1, week_path)) {
            LOGE(kTag, "weekly chart for an unknown ladder level w%u", static_cast<unsigned>(weeks));
            return ChartRead::Failed;
        }
        const char *paths[] = {path, week_path};
        const size_t path_count = weeks > 1 ? 2 : 1;
        for (size_t i = 0; i < path_count; ++i) {
            // The one-week pass only adds what the coarser level does not have yet.
            if (i > 0) pass.start = std::max(pass.start, pass.last_end);
            LongHistoryFiles::ReadStats stats;
            const LongHistoryFiles::ReadResult read =
                LongHistoryFiles::readSpanFile(storage_, paths[i], &chartRecord, &pass, stats);
            if (read == LongHistoryFiles::ReadResult::BadHeader || read == LongHistoryFiles::ReadResult::Failed) {
                LOGE(kTag, "weekly chart read of %s failed (%u bad lines)", paths[i],
                     static_cast<unsigned>(stats.skipped));
                return ChartRead::Failed;
            }
        }
        pass.start = std::max(pass.start, pass.last_end);
        open_writer = &weekly_;
    } else {
        uint32_t first_day = 0;
        uint32_t last_day = 0;
        if (!LocalDate::localDayKey(static_cast<time_t>(start), first_day) ||
            !LocalDate::localDayKey(static_cast<time_t>(end - 1U), last_day)) {
            LOGE(kTag, "daily chart range has no local date");
            return ChartRead::Failed;
        }
        for (uint32_t year = first_day / kDayKeyYearDivisor; year <= last_day / kDayKeyYearDivisor; ++year) {
            DailySummary::ReadStats stats;
            if (DailySummary::readYear(storage_, year, &chartRecord, &pass, stats) !=
                DailySummary::ReadResult::Read) {
                LOGE(kTag, "daily chart read of %u failed (%u bad lines)", static_cast<unsigned>(year),
                     static_cast<unsigned>(stats.skipped));
                return ChartRead::Failed;
            }
        }
        open_writer = &daily_;
    }
    if (open_writer->hasOpenBucket()) {
        const HistoryBucket::Record &open = open_writer->openRecord();
        if (!open.empty() && inChartRange(pass, open)) fn(open, context);
    }
    return ChartRead::Read;
}

bool LongHistory::OpResults::ok() const {
    for (const OpResult &r : items) {
        if (!r.ok) return false;
    }
    return true;
}

void LongHistory::OpResults::add(const char *path, bool existed, bool removed, bool ok) {
    OpResult r;
    r.path = path;
    r.existed = existed;
    r.removed = removed;
    r.ok = ok;
    items.push_back(r);
}

LongHistory::OpStatus LongHistory::beginOp() {
    Lock lock(*this, kWebLockTimeoutMs);
    if (!lock.locked() || op_running_) return OpStatus::Busy;
    if (!enabled_) return OpStatus::Off;
    op_running_ = true;
    setWritersEnabled(false); // drops every in-memory quarter, hour, day and week
    return OpStatus::Done;
}

void LongHistory::endOp() {
    // The main loop holds the lock only for one poll; wait for it.
    for (;;) {
        Lock lock(*this, kWebLockTimeoutMs);
        if (!lock.locked()) {
            LOGW(kTag, "waiting for the long-term history lock to finish an operation");
            continue;
        }
        op_running_ = false;
        setWritersEnabled(enabled_); // prepares and rebuilds from the files left
        return;
    }
}

LongHistory::OpStatus LongHistory::clearAll(OpResults &results) {
    results = OpResults{};
    const OpStatus begun = beginOp();
    if (begun != OpStatus::Done) return begun;
    for (const char *root : treeGroups) {
        const LongHistoryFiles::TreeRemoveResult r = LongHistoryFiles::removeTree(storage_, root);
        results.add(root, r != LongHistoryFiles::TreeRemoveResult::Absent,
                    r == LongHistoryFiles::TreeRemoveResult::Removed,
                    r != LongHistoryFiles::TreeRemoveResult::Failed);
    }
    // The upstream daily history is not running while our log is on; its
    // files are removed too, so "ALL long-term history" is true (Q-D).
    const char *upstream[] = {DailyExtremaHistory::kDailyCsvPath, DailyExtremaHistory::kStatePathA,
                              DailyExtremaHistory::kStatePathB, DailyExtremaHistory::kLegacyStatePath};
    for (const char *path : upstream) {
        bool exists = false;
        size_t size = 0;
        const bool checked = storage_.fileInfo(path, exists, size);
        const bool ok = checked && (!exists || storage_.removeFile(path));
        results.add(path, exists, exists && ok, ok);
    }
    endOp();
    if (!results.ok()) LOGE(kTag, "clear history: some files could not be removed");
    return OpStatus::Done;
}

LongHistory::OpStatus LongHistory::clearToday(time_t now, OpResults &results) {
    results = OpResults{};
    SummaryLevel::Bucket today;
    const uint32_t quarter = static_cast<uint32_t>(now) / QuarterHourRow::kQuarterSeconds *
                             QuarterHourRow::kQuarterSeconds;
    if (!daily_level_.bucketFor(quarter, today)) return OpStatus::NoTime;
    const OpStatus begun = beginOp();
    if (begun != OpStatus::Done) return begun;
    const uint32_t cut = today.start;
    // Every file that can hold something from [cut, now]: the 15-minute files
    // of the (up to two) UTC days, and each level's file at both ends.
    std::vector<std::pair<String, bool>> files; // path, span
    auto addFile = [&files](const char *path, bool span) {
        for (const auto &f : files) {
            if (f.first == path) return;
        }
        files.emplace_back(String(path), span);
    };
    const uint32_t ends[] = {cut, quarter};
    for (uint32_t t : ends) {
        uint32_t utc_day = 0;
        char path[LongHistoryFiles::kPathCapacity];
        if (!LocalDate::utcDayKey(static_cast<time_t>(t), utc_day) ||
            !LongHistoryFiles::quarterFilePath(utc_day, path)) {
            results.add("/aura/history/15min", false, false, false);
            continue;
        }
        addFile(path, false);
        const SummaryLevel *levels[] = {&hourly_level_, &daily_level_, &weekly_level_};
        for (const SummaryLevel *level : levels) {
            SummaryLevel::Bucket b;
            if (level->bucketFor(t, b)) addFile(b.path, true);
        }
    }
    // Coarser week levels are folded from w1; a w1 record cut here may already
    // be in one, so they are cut too and refolded by the next ladder update.
    for (uint32_t weeks = 2; weeks <= WeeklySummary::kMaxLevelWeeks; weeks *= 2) {
        char path[LongHistoryFiles::kPathCapacity];
        if (LongHistoryFiles::weeklyFilePath(weeks, path)) addFile(path, true);
    }
    for (const auto &f : files) {
        const LongHistoryFiles::CutResult r = LongHistoryFiles::cutRowsFrom(storage_, f.first.c_str(), f.second, cut);
        if (r == LongHistoryFiles::CutResult::Absent) continue;
        results.add(f.first.c_str(), true, r == LongHistoryFiles::CutResult::Cut,
                    r != LongHistoryFiles::CutResult::Failed);
    }
    endOp();
    if (!results.ok()) LOGE(kTag, "delete today: some files could not be cut");
    return OpStatus::Done;
}

LongHistory::OpStatus LongHistory::clearFuture(time_t now, OpResults &results) {
    results = OpResults{};
    uint32_t utc_day = 0, local_day = 0;
    if (!time_trusted_.load() || !LocalDate::utcDayKey(now, utc_day) || !LocalDate::localDayKey(now, local_day)) {
        return OpStatus::NoTime;
    }
    const OpStatus begun = beginOp();
    if (begun != OpStatus::Done) return begun;
    const uint32_t cut = static_cast<uint32_t>(now);
    ForwardOnlyGuard::ClearStats stats;
    const bool quarter_ok = ForwardOnlyGuard::clearFutureQuarterRows(storage_, now, stats) ==
                            ForwardOnlyGuard::ClearResult::Cleared;
    results.add(LongHistoryFiles::kQuarterRoot, stats.files_removed + stats.rows_removed != 0,
                stats.files_removed + stats.rows_removed != 0, quarter_ok);
    // Later summary files go whole; the current one is cut at its first
    // record that ends after now.
    char hourly[LongHistoryFiles::kPathCapacity], daily[LongHistoryFiles::kPathCapacity];
    const bool paths = LongHistoryFiles::hourlyFilePath(utc_day / 10000U, (utc_day / 100U) % 100U, hourly) &&
                       LongHistoryFiles::dailyFilePath(local_day / 10000U, daily);
    if (!paths) {
        results.add("/aura/history", false, false, false);
    } else {
        const std::pair<const char *, const char *> dirs[] = {{LongHistoryFiles::kHourlyRoot, hourly},
                                                              {LongHistoryFiles::kDailyRoot, daily}};
        for (const auto &d : dirs) {
            const char *name = strrchr(d.second, '/') + 1;
            uint32_t removed = 0;
            const LongHistoryFiles::TreeRemoveResult r =
                LongHistoryFiles::removeFilesAfter(storage_, d.first, name, removed);
            if (r == LongHistoryFiles::TreeRemoveResult::Absent) continue;
            results.add(d.first, removed != 0, removed != 0, r != LongHistoryFiles::TreeRemoveResult::Failed);
        }
        std::vector<String> files = {String(hourly), String(daily)};
        for (uint32_t weeks = 1; weeks <= WeeklySummary::kMaxLevelWeeks; weeks *= 2) {
            char path[LongHistoryFiles::kPathCapacity];
            if (LongHistoryFiles::weeklyFilePath(weeks, path)) files.emplace_back(path);
        }
        for (const String &f : files) {
            const LongHistoryFiles::CutResult r = LongHistoryFiles::cutRowsFrom(storage_, f.c_str(), true, cut);
            if (r == LongHistoryFiles::CutResult::Absent) continue;
            results.add(f.c_str(), true, r == LongHistoryFiles::CutResult::Cut,
                        r != LongHistoryFiles::CutResult::Failed);
        }
    }
    endOp();
    if (!results.ok()) LOGE(kTag, "clear future-dated history: some files could not be changed");
    return OpStatus::Done;
}
