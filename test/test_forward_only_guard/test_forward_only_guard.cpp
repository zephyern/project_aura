// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string>

#include "ArduinoMock.h"
#include "SdTempRoot.h"
#include "core/Logger.h"
#include "core/WakePowerGuard.h"
#include "modules/ForwardOnlyGuard.h"
#include "modules/LongHistoryFiles.h"
#include "modules/SdCardManager.h"

using namespace ForwardOnlyGuard;

namespace {

constexpr uint32_t kDay = 1794355200; // 2026-11-11T00:00:00Z
constexpr uint32_t kQ = QuarterHourRow::kQuarterSeconds;
constexpr const char *kToday = "/aura/history/15min/2026/11/2026-11-11.csv";
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;

SdTempRoot *g_root = nullptr;

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

std::string dayFile(uint32_t day_start, uint32_t quarters) {
    std::string out = header();
    for (uint32_t q = 0; q < quarters; ++q) out += row(day_start + q * kQ);
    return out;
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

void test_equal_and_earlier_epochs_are_refused_later_accepted() {
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::Accept), static_cast<int>(accept(0, kDay).verdict));
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::Accept), static_cast<int>(accept(kDay, kDay + kQ).verdict));
    const Decision same = accept(kDay, kDay);
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::ClockBehind), static_cast<int>(same.verdict));
    TEST_ASSERT_EQUAL_UINT32(kDay, same.last_epoch);
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::ClockBehind),
                      static_cast<int>(accept(kDay, kDay - kQ).verdict));
}

void test_restart_with_clock_slightly_behind_pauses_then_resumes() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write(kToday, dayFile(kDay, 8)); // last row starts at kDay + 7q
    uint32_t last = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(LastResult::Found),
                      static_cast<int>(lastQuarterEpoch(sd, kToday, last)));
    TEST_ASSERT_EQUAL_UINT32(kDay + 7 * kQ, last);
    // The RTC came back 20 minutes behind: the next quarters are refused...
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::ClockBehind),
                      static_cast<int>(accept(last, kDay + 6 * kQ).verdict));
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::ClockBehind),
                      static_cast<int>(accept(last, kDay + 7 * kQ).verdict));
    // ...until the clock passes the last record; a jump forward leaves a gap.
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::Accept),
                      static_cast<int>(accept(last, kDay + 8 * kQ).verdict));
    TEST_ASSERT_EQUAL(static_cast<int>(Verdict::Accept),
                      static_cast<int>(accept(last, kDay + 40 * kQ).verdict));
}

void test_last_epoch_ignores_a_bad_last_line_and_reports_no_records() {
    SdCardManager sd;
    g_root->mount(sd);
    uint32_t last = 123;
    TEST_ASSERT_EQUAL(static_cast<int>(LastResult::NoRecords),
                      static_cast<int>(lastQuarterEpoch(sd, kToday, last)));
    g_root->write(kToday, header());
    TEST_ASSERT_EQUAL(static_cast<int>(LastResult::NoRecords),
                      static_cast<int>(lastQuarterEpoch(sd, kToday, last)));
    TEST_ASSERT_EQUAL_UINT32(123, last);
    g_root->write(kToday, dayFile(kDay, 3) + "1794358800,bad\n");
    TEST_ASSERT_EQUAL(static_cast<int>(LastResult::Found),
                      static_cast<int>(lastQuarterEpoch(sd, kToday, last)));
    TEST_ASSERT_EQUAL_UINT32(kDay + 2 * kQ, last);
    g_root->write(kToday, "date,metric\n" + row(kDay));
    TEST_ASSERT_EQUAL(static_cast<int>(LastResult::Unsupported),
                      static_cast<int>(lastQuarterEpoch(sd, kToday, last)));
}

void test_clear_future_removes_only_future_rows_and_files() {
    SdCardManager sd;
    g_root->mount(sd);
    const std::string yesterday = "/aura/history/15min/2026/11/2026-11-10.csv";
    g_root->write(yesterday, dayFile(kDay - 86400, 96));
    g_root->write(kToday, dayFile(kDay, 40));
    g_root->write("/aura/history/15min/2026/11/2026-11-12.csv", dayFile(kDay + 86400, 4));
    g_root->write("/aura/history/15min/2026/12/2026-12-25.csv", dayFile(1798156800, 4));
    g_root->write("/aura/history/15min/2031/02/2031-02-01.csv", dayFile(1927670400, 4));
    // Now is 05:00 UTC: quarters 0..20 (start <= now) stay, 21..39 go.
    const time_t now = kDay + 20 * kQ;
    ClearStats stats;
    TEST_ASSERT_EQUAL(static_cast<int>(ClearResult::Cleared),
                      static_cast<int>(clearFutureQuarterRows(sd, now, stats)));
    TEST_ASSERT_EQUAL_UINT32(3, stats.files_removed);
    TEST_ASSERT_EQUAL_UINT32(19, stats.rows_removed);
    TEST_ASSERT_EQUAL_STRING(dayFile(kDay, 21).c_str(), g_root->read(kToday).c_str());
    TEST_ASSERT_EQUAL_STRING(dayFile(kDay - 86400, 96).c_str(), g_root->read(yesterday).c_str());
    TEST_ASSERT_FALSE(g_root->exists("/aura/history/15min/2026/11/2026-11-12.csv"));
    TEST_ASSERT_FALSE(g_root->exists("/aura/history/15min/2026/12"));
    TEST_ASSERT_FALSE(g_root->exists("/aura/history/15min/2031"));
    TEST_ASSERT_TRUE(g_root->exists("/aura/history/15min/2026/11"));
    // Nothing in the future: a second run changes nothing.
    TEST_ASSERT_EQUAL(static_cast<int>(ClearResult::Cleared),
                      static_cast<int>(clearFutureQuarterRows(sd, now, stats)));
    TEST_ASSERT_EQUAL_UINT32(0, stats.files_removed);
    TEST_ASSERT_EQUAL_UINT32(0, stats.rows_removed);
}

void test_clear_future_needs_trusted_time() {
    SdCardManager sd;
    g_root->mount(sd);
    ClearStats stats;
    TEST_ASSERT_EQUAL(static_cast<int>(ClearResult::Failed),
                      static_cast<int>(clearFutureQuarterRows(sd, 1000, stats)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_equal_and_earlier_epochs_are_refused_later_accepted);
    RUN_TEST(test_restart_with_clock_slightly_behind_pauses_then_resumes);
    RUN_TEST(test_last_epoch_ignores_a_bad_last_line_and_reports_no_records);
    RUN_TEST(test_clear_future_removes_only_future_rows_and_files);
    RUN_TEST(test_clear_future_needs_trusted_time);
    return UNITY_END();
}
