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
#include "modules/LongHistoryFiles.h"

namespace {

constexpr const char *kTag = "History";
constexpr uint32_t kWebLockTimeoutMs = 5000;
constexpr uint32_t kIntervalLockTimeoutMs = 1000;

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
    Lock lock(self, kIntervalLockTimeoutMs);
    if (!lock.locked()) {
        LOGE(kTag, "long-term history busy; five-minute interval %u not recorded",
             static_cast<unsigned>(interval.entry.start_epoch));
        return;
    }
    self.logger_.onInterval(interval);
}

void LongHistory::poll(time_t now, bool time_trusted) {
    Lock lock(*this, 0); // a web operation holds it only briefly; try again next pass
    if (!lock.locked() || op_running_ || !enabled_) return;
    logger_.poll(now, time_trusted);
    const bool ready = logger_.prepared();
    hourly_.poll(now, time_trusted, ready);
    daily_.poll(now, time_trusted, ready);
    weekly_.poll(now, time_trusted, ready);
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

bool LongHistory::firstDay(char *out, size_t len) const {
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
    uint32_t day_key = 0;
    if (!recordDayKey(first.row, day_key)) return false;
    LocalDate::formatDayKey(day_key, out, len);
    return true;
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
