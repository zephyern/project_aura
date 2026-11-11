// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string>
#include <vector>

#include "ArduinoMock.h"
#include "SdTempRoot.h"
#include "config/AppConfig.h"
#include "core/Logger.h"
#include "core/WakePowerGuard.h"
#include "modules/LongHistoryFiles.h"
#include "modules/QuarterHourLogger.h"
#include "modules/SdCardManager.h"

namespace {

constexpr uint32_t kDay = 1794355200; // 2026-11-11T00:00:00Z
constexpr uint32_t kQ = QuarterHourRow::kQuarterSeconds;
constexpr uint32_t kStep = ChartsHistory::kStepSeconds;
constexpr const char *kToday = "/aura/history/15min/2026/11/2026-11-11.csv";
constexpr const char *kYesterday = "/aura/history/15min/2026/11/2026-11-10.csv";
constexpr int CO2 = ChartsHistory::METRIC_CO2;
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;
constexpr int GAS = ChartsHistory::METRIC_OPTIONAL_GAS;

SdTempRoot *g_root = nullptr;

ChartsHistory::CompletedInterval interval(uint32_t start, double temp_sum, uint16_t count,
                                          float lo, uint16_t lo_s, float hi, uint16_t hi_s) {
    ChartsHistory::CompletedInterval iv;
    iv.entry.start_epoch = start;
    iv.entry.kind = ChartsHistory::Summary;
    iv.entry.valid_mask = ChartsHistory::metricBit(ChartsHistory::METRIC_TEMPERATURE);
    iv.entry.samples[TEMP] = count;
    iv.entry.minimum[TEMP] = lo;
    iv.entry.maximum[TEMP] = hi;
    iv.entry.values[TEMP] = static_cast<float>(temp_sum / count);
    iv.sums[TEMP] = temp_sum;
    iv.min_second[TEMP] = lo_s;
    iv.max_second[TEMP] = hi_s;
    return iv;
}

ChartsHistory::CompletedInterval simple(uint32_t start, float value) {
    return interval(start, value * 10.0, 10, value, 0, value, 0);
}

struct Rows {
    std::vector<QuarterHourRow::Row> rows;
};

bool collect(const QuarterHourRow::Row &row, size_t, void *context) {
    static_cast<Rows *>(context)->rows.push_back(row);
    return true;
}

Rows readRows(SdCardManager &sd, const char *path) {
    Rows out;
    LongHistoryFiles::ReadStats stats;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistoryFiles::ReadResult::Read),
                      static_cast<int>(LongHistoryFiles::readQuarterFile(sd, path, &collect, &out, stats)));
    TEST_ASSERT_EQUAL_UINT32(0, stats.skipped);
    return out;
}

std::string header() {
    String h;
    QuarterHourRow::appendHeader(h);
    return h;
}

std::string row(uint32_t start) {
    QuarterHourRow::Row r;
    r.start_epoch = start;
    r.metrics[TEMP].sum = 20;
    r.metrics[TEMP].count = 1;
    r.metrics[TEMP].min = r.metrics[TEMP].max = 20;
    r.metrics[TEMP].min_epoch = r.metrics[TEMP].max_epoch = start;
    String out;
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourRow::EncodeResult::Ok),
                      static_cast<int>(QuarterHourRow::appendRow(r, out)));
    return out;
}

void startLogger(QuarterHourLogger &logger, uint32_t now) {
    logger.setEnabled(true);
    logger.poll(static_cast<time_t>(now), true);
}

} // namespace

void setUp() {
    setMillis(0);
    WakePowerGuard::resetForTest();
    Logger::resetRecentForTest();
    g_root = new SdTempRoot();
}

void tearDown() {
    delete g_root;
    g_root = nullptr;
    WakePowerGuard::resetForTest();
}

void test_setting_defaults_off_and_off_writes_nothing() {
    const Config::StoredConfig defaults{};
    TEST_ASSERT_FALSE(defaults.history_log_enabled);

    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourLogger::State::Off),
                      static_cast<int>(logger.status().state));
    logger.poll(static_cast<time_t>(kDay), true);
    for (uint32_t s = 0; s < 3; ++s) logger.onInterval(simple(kDay + s * kStep, 20.0f));
    TEST_ASSERT_FALSE(g_root->exists(kToday));
    TEST_ASSERT_EQUAL_UINT32(0, logger.status().rows_written);
}

void test_three_intervals_make_one_exact_row() {
    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    startLogger(logger, kDay);
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourLogger::State::Recording),
                      static_cast<int>(logger.status().state));

    const uint32_t q = kDay + 4 * kQ; // 01:00Z
    logger.onInterval(interval(q, 200.25, 10, 19.5f, 40, 20.5f, 10));
    logger.onInterval(interval(q + kStep, 210.0, 10, 21.0f, 0, 21.0f, 0));
    TEST_ASSERT_FALSE(g_root->exists(kToday)); // still open
    logger.onInterval(interval(q + 2 * kStep, 190.125, 9, 19.5f, 299, 22.0f, 7));

    TEST_ASSERT_EQUAL_STRING(header().c_str(), g_root->read(kToday).substr(0, header().size()).c_str());
    const Rows got = readRows(sd, kToday);
    TEST_ASSERT_EQUAL_UINT32(1, got.rows.size());
    const QuarterHourRow::Row &r = got.rows[0];
    TEST_ASSERT_EQUAL_UINT32(q, r.start_epoch);
    TEST_ASSERT_TRUE(r.metrics[TEMP].sum == 200.25 + 210.0 + 190.125);
    TEST_ASSERT_EQUAL_UINT32(29, r.metrics[TEMP].count);
    TEST_ASSERT_TRUE(r.metrics[TEMP].min == 19.5f);
    // The low of 19.5 was first reached in the first interval, second 40.
    TEST_ASSERT_EQUAL_UINT32(q + 40, r.metrics[TEMP].min_epoch);
    TEST_ASSERT_TRUE(r.metrics[TEMP].max == 22.0f);
    TEST_ASSERT_EQUAL_UINT32(q + 2 * kStep + 7, r.metrics[TEMP].max_epoch);
    TEST_ASSERT_TRUE(r.metrics[CO2].empty());
    TEST_ASSERT_EQUAL_UINT32(q, logger.status().last_epoch);
    TEST_ASSERT_EQUAL_UINT32(1, logger.status().rows_written);
}

void test_gap_quarters_get_no_row_and_partial_quarter_closes_on_later_interval() {
    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    startLogger(logger, kDay);

    logger.onInterval(simple(kDay + kStep, 20.0f));      // quarter 0, middle slot only
    logger.onInterval(simple(kDay + 3 * kQ, 21.0f));     // quarter 3: closes quarter 0
    TEST_ASSERT_EQUAL_UINT32(1, readRows(sd, kToday).rows.size());
    // poll() closes quarter 3 only after its end plus the grace period.
    logger.poll(static_cast<time_t>(kDay + 4 * kQ + QuarterHourLogger::kCloseGraceSeconds - 1), true);
    TEST_ASSERT_EQUAL_UINT32(1, readRows(sd, kToday).rows.size());
    logger.poll(static_cast<time_t>(kDay + 4 * kQ + QuarterHourLogger::kCloseGraceSeconds), false);
    TEST_ASSERT_EQUAL_UINT32(1, readRows(sd, kToday).rows.size()); // untrusted time ignored
    logger.poll(static_cast<time_t>(kDay + 4 * kQ + QuarterHourLogger::kCloseGraceSeconds), true);
    const Rows got = readRows(sd, kToday);
    TEST_ASSERT_EQUAL_UINT32(2, got.rows.size());
    TEST_ASSERT_EQUAL_UINT32(kDay, got.rows[0].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(kDay + 3 * kQ, got.rows[1].start_epoch); // quarters 1-2: no row

    // A late interval of a closed quarter is refused, not written twice.
    logger.onInterval(simple(kDay + 3 * kQ + 2 * kStep, 22.0f));
    TEST_ASSERT_EQUAL_UINT32(2, readRows(sd, kToday).rows.size());
    TEST_ASSERT_EQUAL_UINT32(1, logger.status().quarters_refused);
}

void test_restart_mid_quarter_repairs_tail_and_continues() {
    SdCardManager sd;
    g_root->mount(sd);
    // Before the restart: two rows and a torn third (power cut during append).
    g_root->write(kToday, header() + row(kDay) + row(kDay + kQ) + "17940");
    QuarterHourLogger logger(sd);
    startLogger(logger, kDay + 2 * kQ + 400);
    TEST_ASSERT_EQUAL_UINT32(kDay + kQ, logger.status().last_epoch);
    TEST_ASSERT_EQUAL_STRING((header() + row(kDay) + row(kDay + kQ)).c_str(), g_root->read(kToday).c_str());

    // The quarter that was open at the cut lost its first slot; the rest is kept.
    logger.onInterval(simple(kDay + 2 * kQ + kStep, 20.0f));
    logger.onInterval(simple(kDay + 2 * kQ + 2 * kStep, 22.0f));
    const Rows got = readRows(sd, kToday);
    TEST_ASSERT_EQUAL_UINT32(3, got.rows.size());
    TEST_ASSERT_EQUAL_UINT32(kDay + 2 * kQ, got.rows[2].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(20, got.rows[2].metrics[TEMP].count);
}

void test_clock_behind_is_refused_with_status() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write(kToday, header() + row(kDay + 8 * kQ));
    QuarterHourLogger logger(sd);
    startLogger(logger, kDay + 6 * kQ);
    for (uint32_t s = 0; s < 3; ++s) logger.onInterval(simple(kDay + 6 * kQ + s * kStep, 20.0f));
    QuarterHourLogger::Status st = logger.status();
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourLogger::State::ClockBehind), static_cast<int>(st.state));
    TEST_ASSERT_EQUAL_UINT32(kDay + 8 * kQ, st.last_epoch);
    TEST_ASSERT_EQUAL_UINT32(1, readRows(sd, kToday).rows.size());

    // Once the clock is past the last row, recording resumes (with a gap).
    for (uint32_t s = 0; s < 3; ++s) logger.onInterval(simple(kDay + 9 * kQ + s * kStep, 20.0f));
    st = logger.status();
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourLogger::State::Recording), static_cast<int>(st.state));
    TEST_ASSERT_EQUAL_UINT32(2, readRows(sd, kToday).rows.size());
}

void test_utc_midnight_switches_file() {
    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    startLogger(logger, kDay - kQ);
    for (uint32_t s = 0; s < 3; ++s) logger.onInterval(simple(kDay - kQ + s * kStep, 20.0f));
    for (uint32_t s = 0; s < 3; ++s) logger.onInterval(simple(kDay + s * kStep, 21.0f));
    const Rows before = readRows(sd, kYesterday);
    const Rows after = readRows(sd, kToday);
    TEST_ASSERT_EQUAL_UINT32(1, before.rows.size());
    TEST_ASSERT_EQUAL_UINT32(kDay - kQ, before.rows[0].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(1, after.rows.size());
    TEST_ASSERT_EQUAL_UINT32(kDay, after.rows[0].start_epoch);
}

void test_no_card_and_write_failure_are_typed() {
    SdCardManager missing; // never mounted
    QuarterHourLogger offline(missing);
    startLogger(offline, kDay);
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourLogger::State::NoCard),
                      static_cast<int>(offline.status().state));

    SdCardManager sd;
    g_root->mount(sd);
    // A plain file where the month directory must be: the append cannot open.
    g_root->write("/aura/history/15min/2026/11", "x");
    QuarterHourLogger logger(sd);
    startLogger(logger, kDay);
    for (uint32_t s = 0; s < 3; ++s) logger.onInterval(simple(kDay + s * kStep, 20.0f));
    const QuarterHourLogger::Status st = logger.status();
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourLogger::State::WriteError), static_cast<int>(st.state));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(DailyStorageFailureKind::None), static_cast<int>(st.failure_kind));
    TEST_ASSERT_EQUAL_UINT32(0, st.rows_written);
}

void test_optional_gas_change_within_quarter_keeps_the_new_gas() {
    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    startLogger(logger, kDay);
    for (uint32_t s = 0; s < 3; ++s) {
        ChartsHistory::CompletedInterval iv = simple(kDay + s * kStep, 20.0f);
        iv.entry.valid_mask |= ChartsHistory::metricBit(ChartsHistory::METRIC_OPTIONAL_GAS);
        iv.entry.optional_gas_type = s == 0 ? 1 : 2;
        iv.entry.samples[GAS] = 5;
        iv.entry.minimum[GAS] = iv.entry.maximum[GAS] = 1.0f + s;
        iv.sums[GAS] = 5.0 * (1.0 + s);
        logger.onInterval(iv);
    }
    const Rows got = readRows(sd, kToday);
    TEST_ASSERT_EQUAL_UINT32(1, got.rows.size());
    TEST_ASSERT_EQUAL_UINT8(2, got.rows[0].metrics[GAS].source);
    TEST_ASSERT_EQUAL_UINT32(10, got.rows[0].metrics[GAS].count);
    TEST_ASSERT_EQUAL_UINT32(30, got.rows[0].metrics[TEMP].count);
}

void test_attaches_as_the_charts_listener() {
    SdCardManager sd;
    QuarterHourLogger logger(sd);
    ChartsHistory history;
    TEST_ASSERT_TRUE(logger.attach(history));
    QuarterHourLogger second(sd);
    TEST_ASSERT_FALSE(second.attach(history));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_setting_defaults_off_and_off_writes_nothing);
    RUN_TEST(test_three_intervals_make_one_exact_row);
    RUN_TEST(test_gap_quarters_get_no_row_and_partial_quarter_closes_on_later_interval);
    RUN_TEST(test_restart_mid_quarter_repairs_tail_and_continues);
    RUN_TEST(test_clock_behind_is_refused_with_status);
    RUN_TEST(test_utc_midnight_switches_file);
    RUN_TEST(test_no_card_and_write_failure_are_typed);
    RUN_TEST(test_optional_gas_change_within_quarter_keeps_the_new_gas);
    RUN_TEST(test_attaches_as_the_charts_listener);
    return UNITY_END();
}
