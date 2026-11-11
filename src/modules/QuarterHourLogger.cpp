// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/QuarterHourLogger.h"

#include "config/AppConfig.h"
#include "core/LocalDate.h"
#include "core/Logger.h"
#include "modules/ForwardOnlyGuard.h"
#include "modules/HistoryAggregate.h"
#include "modules/LongHistoryFiles.h"

namespace {

constexpr const char *kTag = "History";
constexpr uint32_t kQuarter = QuarterHourRow::kQuarterSeconds;
constexpr uint32_t kLastSlotOffset = kQuarter - ChartsHistory::kStepSeconds;

} // namespace

QuarterHourLogger::QuarterHourLogger(DailyHistoryStorage &storage) : storage_(storage) {}

void QuarterHourLogger::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    open_ = false;
    prepared_ = false;
    closed_start_ = 0;
    status_ = Status{};
    status_.state = enabled ? State::NoCard : State::Off;
}

bool QuarterHourLogger::attach(ChartsHistory &history) {
    return history.setIntervalListener(&QuarterHourLogger::intervalThunk, this);
}

void QuarterHourLogger::intervalThunk(const ChartsHistory::CompletedInterval &interval,
                                      void *context) {
    static_cast<QuarterHourLogger *>(context)->onInterval(interval);
}

void QuarterHourLogger::fail(DailyStorageFailureKind kind) {
    status_.state = State::WriteError;
    status_.failure_kind = kind;
    // A failed append may have left part of a row: repair before the next one.
    prepared_ = false;
}

bool QuarterHourLogger::prepare() {
    uint32_t day = 0;
    const LongHistoryFiles::FindResult found = LongHistoryFiles::findNewestQuarterFile(storage_, day);
    if (found == LongHistoryFiles::FindResult::Failed) {
        fail(storage_.lastFailureKind());
        return false;
    }
    uint32_t last = 0;
    if (found == LongHistoryFiles::FindResult::Found) {
        char path[LongHistoryFiles::kPathCapacity];
        if (!LongHistoryFiles::quarterFilePath(day, path)) {
            fail(DailyStorageFailureKind::Invalid);
            return false;
        }
        String header;
        QuarterHourRow::appendHeader(header);
        size_t removed = 0;
        if (LongHistoryFiles::repairTail(storage_, path, header.c_str(), removed) ==
            LongHistoryFiles::RepairResult::Failed) {
            fail(storage_.lastFailureKind());
            return false;
        }
        const ForwardOnlyGuard::LastResult read = ForwardOnlyGuard::lastQuarterEpoch(storage_, path, last);
        if (read == ForwardOnlyGuard::LastResult::Unsupported ||
            read == ForwardOnlyGuard::LastResult::Failed) {
            // An unreadable newest file must not be appended to or overtaken
            // silently; it needs attention (or Clear history).
            LOGE(kTag, "cannot read the last row of %s; long-term history not recording", path);
            fail(read == ForwardOnlyGuard::LastResult::Unsupported ? DailyStorageFailureKind::Invalid
                                                                   : storage_.lastFailureKind());
            return false;
        }
    }
    status_.last_epoch = last;
    status_.failure_kind = DailyStorageFailureKind::None;
    status_.state = State::Recording;
    prepared_ = true;
    return true;
}

void QuarterHourLogger::poll(time_t now, bool time_trusted) {
    if (!enabled_) return;
    if (!storage_.isReady()) {
        // Another card may be inserted: prepare it from scratch.
        prepared_ = false;
        open_ = false;
        closed_start_ = 0;
        status_.state = State::NoCard;
        return;
    }
    if (!prepared_ && !prepare()) return;
    if (open_ && time_trusted && now > Config::TIME_VALID_EPOCH &&
        static_cast<uint64_t>(now) >=
            static_cast<uint64_t>(quarter_.start_epoch) + kQuarter + kCloseGraceSeconds) {
        closeQuarter();
    }
}

void QuarterHourLogger::openQuarter(uint32_t start) {
    quarter_ = QuarterHourRow::Row{};
    quarter_.start_epoch = start;
    open_ = true;
}

void QuarterHourLogger::onInterval(const ChartsHistory::CompletedInterval &interval) {
    if (!enabled_ || !prepared_) return;
    const ChartsHistory::Entry &e = interval.entry;
    const uint32_t start = e.start_epoch - e.start_epoch % kQuarter;
    // A late interval of a quarter already written (or closed by poll()).
    if (!open_ && closed_start_ != 0 && start <= closed_start_) {
        ++status_.quarters_refused;
        LOGW(kTag, "interval %u arrived after its quarter %u was closed; not recorded",
             static_cast<unsigned>(e.start_epoch), static_cast<unsigned>(start));
        return;
    }
    if (open_ && start < quarter_.start_epoch) {
        ++status_.quarters_refused;
        LOGW(kTag, "interval %u is older than the open quarter %u; not recorded",
             static_cast<unsigned>(e.start_epoch), static_cast<unsigned>(quarter_.start_epoch));
        return;
    }
    if (open_ && start > quarter_.start_epoch) closeQuarter();
    if (!open_) openQuarter(start);

    for (int m = 0; m < ChartsHistory::kMetricCount; ++m) {
        if (!(e.valid_mask & ChartsHistory::metricBit(static_cast<ChartsHistory::Metric>(m))) ||
            e.samples[m] == 0) {
            continue;
        }
        HistoryAggregate add;
        add.sum = interval.sums[m];
        add.count = e.samples[m];
        add.min = e.minimum[m];
        add.max = e.maximum[m];
        add.min_epoch = e.start_epoch + interval.min_second[m];
        add.max_epoch = e.start_epoch + interval.max_second[m];
        if (m == ChartsHistory::METRIC_OPTIONAL_GAS) {
            if (e.optional_gas_type == 0) {
                LOGW(kTag, "optional gas samples at %u without a gas type; not recorded",
                     static_cast<unsigned>(e.start_epoch));
                continue;
            }
            add.source = e.optional_gas_type;
        } else if (m == ChartsHistory::METRIC_OUTDOOR_TEMP) {
            add.source = e.outdoor_source;  // validEntry() guarantees a known source
        }
        HistoryAggregate &into = quarter_.metrics[m];
        const uint8_t previous_source = into.source;
        const HistoryMergeResult r = HistoryAggregates::mergeNewer(into, add);
        if (r == HistoryMergeResult::Replaced) {
            LOGW(kTag, "metric %d source changed from %u to %u in quarter %u; earlier part dropped", m,
                 static_cast<unsigned>(previous_source), static_cast<unsigned>(add.source),
                 static_cast<unsigned>(quarter_.start_epoch));
        } else if (r == HistoryMergeResult::CountOverflow) {
            LOGE(kTag, "sample count overflow in quarter %u; interval dropped",
                 static_cast<unsigned>(quarter_.start_epoch));
        }
    }
    if (e.start_epoch == quarter_.start_epoch + kLastSlotOffset) closeQuarter();
}

void QuarterHourLogger::closeQuarter() {
    open_ = false;
    closed_start_ = quarter_.start_epoch;
    bool any = false;
    for (const HistoryAggregate &a : quarter_.metrics) any = any || !a.empty();
    if (!any) return; // a quarter without data gets no row

    const ForwardOnlyGuard::Decision d =
        ForwardOnlyGuard::accept(status_.last_epoch, quarter_.start_epoch);
    if (d.verdict == ForwardOnlyGuard::Verdict::ClockBehind) {
        ++status_.quarters_refused;
        status_.state = State::ClockBehind;
        LOGW(kTag, "quarter %u not after the last row %u: clock behind, not recorded",
             static_cast<unsigned>(quarter_.start_epoch), static_cast<unsigned>(d.last_epoch));
        return;
    }
    uint32_t day = 0;
    char path[LongHistoryFiles::kPathCapacity];
    if (!LocalDate::utcDayKey(quarter_.start_epoch, day) || !LongHistoryFiles::quarterFilePath(day, path)) {
        LOGE(kTag, "quarter %u has no valid UTC date; not recorded",
             static_cast<unsigned>(quarter_.start_epoch));
        fail(DailyStorageFailureKind::Invalid);
        return;
    }
    String text;
    bool exists = false;
    size_t size = 0;
    if (!storage_.fileInfo(path, exists, size)) {
        fail(storage_.lastFailureKind());
        return;
    }
    if (!exists || size == 0) QuarterHourRow::appendHeader(text);
    const QuarterHourRow::EncodeResult encoded = QuarterHourRow::appendRow(quarter_, text);
    if (encoded != QuarterHourRow::EncodeResult::Ok) {
        LOGE(kTag, "quarter %u could not be encoded (%u); not recorded",
             static_cast<unsigned>(quarter_.start_epoch), static_cast<unsigned>(encoded));
        fail(DailyStorageFailureKind::Invalid);
        return;
    }
    if (!storage_.appendText(path, text.c_str())) {
        LOGW(kTag, "append to %s failed; quarter %u lost", path,
             static_cast<unsigned>(quarter_.start_epoch));
        fail(storage_.lastFailureKind());
        return;
    }
    status_.last_epoch = quarter_.start_epoch;
    status_.failure_kind = DailyStorageFailureKind::None;
    status_.state = State::Recording;
    ++status_.rows_written;
    for (const RowListenerSlot &slot : row_listeners_) {
        if (slot.fn) slot.fn(quarter_, slot.context);
    }
}

bool QuarterHourLogger::addRowListener(RowListener listener, void *context) {
    if (!listener) return false;
    for (const RowListenerSlot &slot : row_listeners_) {
        if (slot.fn == listener && slot.context == context) return false;
    }
    for (RowListenerSlot &slot : row_listeners_) {
        if (!slot.fn) {
            slot.fn = listener;
            slot.context = context;
            return true;
        }
    }
    return false;
}
