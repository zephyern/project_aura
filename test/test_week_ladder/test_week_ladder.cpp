// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string>
#include <vector>

#include "ArduinoMock.h"
#include "SdTempRoot.h"
#include "core/Logger.h"
#include "core/WakePowerGuard.h"
#include "modules/LongHistoryFiles.h"
#include "modules/QuarterHourLogger.h"
#include "modules/SdCardManager.h"
#include "modules/SummaryWriter.h"
#include "modules/WeeklySummary.h"

namespace {

constexpr uint32_t kA = WeeklySummary::kAnchorEpoch;
constexpr uint32_t kW = WeeklySummary::kWeekSeconds;
constexpr uint32_t kQ = QuarterHourRow::kQuarterSeconds;
constexpr const char *kW1 = "/aura/history/weeks/w1.csv";
constexpr const char *kW2 = "/aura/history/weeks/w2.csv";
constexpr const char *kW4 = "/aura/history/weeks/w4.csv";
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;

SdTempRoot *g_root = nullptr;
const WeeklySummary::Level kLevel;

QuarterHourRow::SpanRow week(uint32_t index) {
    QuarterHourRow::SpanRow r;
    r.start_epoch = kA + index * kW;
    r.end_epoch = r.start_epoch + kW;
    HistoryAggregate &a = r.metrics[TEMP];
    a.count = 100 + index;
    a.sum = 20.5 * a.count;
    a.min = 10.0f + static_cast<float>(index % 7);
    a.max = 30.0f;
    a.min_epoch = r.start_epoch + 60;
    a.max_epoch = r.start_epoch + 120;
    return r;
}

std::string weekFile(uint32_t from, uint32_t to) {
    String text;
    QuarterHourRow::appendSpanHeader(text);
    for (uint32_t i = from; i < to; ++i) {
        TEST_ASSERT_EQUAL(0, static_cast<int>(QuarterHourRow::appendSpanRow(week(i), text)));
    }
    return text;
}

std::vector<QuarterHourRow::SpanRow> g_read;
bool collect(const QuarterHourRow::SpanRow &row, size_t, void *) {
    g_read.push_back(row);
    return true;
}
std::vector<QuarterHourRow::SpanRow> readLevel(SdCardManager &sd, const char *path) {
    g_read.clear();
    LongHistoryFiles::ReadStats st;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistoryFiles::ReadResult::Read),
                      static_cast<int>(LongHistoryFiles::readSpanFile(sd, path, &collect, nullptr, st)));
    return g_read;
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

void test_bucket_bounds_are_anchored_at_monday() {
    uint32_t s = 0, e = 0;
    TEST_ASSERT_FALSE(WeeklySummary::bucketBounds(1, kA - 1, s, e));
    TEST_ASSERT_FALSE(WeeklySummary::bucketBounds(3, kA, s, e));
    TEST_ASSERT_TRUE(WeeklySummary::bucketBounds(1, kA + kW + 5, s, e));
    TEST_ASSERT_EQUAL_UINT32(kA + kW, s);
    TEST_ASSERT_EQUAL_UINT32(kA + 2 * kW, e);
    TEST_ASSERT_TRUE(WeeklySummary::bucketBounds(4, kA + 5 * kW, s, e));
    TEST_ASSERT_EQUAL_UINT32(kA + 4 * kW, s);
    TEST_ASSERT_EQUAL_UINT32(kA + 8 * kW, e);
}

void test_weekly_writer_from_quarter_rows() {
    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    SummaryWriter weekly(sd, kLevel);
    logger.setEnabled(true);
    weekly.setEnabled(true);
    const uint32_t start = kA + 40 * kW;
    logger.poll(static_cast<time_t>(start), true);
    weekly.poll(static_cast<time_t>(start), true, logger.prepared());
    for (uint32_t t = start; t < start + kW + kQ; t += kQ) {
        QuarterHourRow::Row r;
        r.start_epoch = t;
        r.metrics[TEMP].count = 3;
        r.metrics[TEMP].sum = 60.0;
        r.metrics[TEMP].min = 20.0f;
        r.metrics[TEMP].max = 20.0f;
        r.metrics[TEMP].min_epoch = t;
        r.metrics[TEMP].max_epoch = t;
        weekly.onRow(r);
    }
    const auto rows = readLevel(sd, kW1);
    TEST_ASSERT_EQUAL_UINT32(1, rows.size());
    TEST_ASSERT_EQUAL_UINT32(start, rows[0].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(start + kW, rows[0].end_epoch);
    TEST_ASSERT_EQUAL_UINT32(672 * 3, rows[0].metrics[TEMP].count);
}

void test_no_coarser_level_at_or_below_the_threshold() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write(kW1, weekFile(0, WeeklySummary::kMaxPointsPerLevel));
    WeeklySummary::LadderStats st;
    TEST_ASSERT_EQUAL(0, static_cast<int>(WeeklySummary::updateLadder(sd, st)));
    TEST_ASSERT_FALSE(g_root->exists(kW2));
    TEST_ASSERT_EQUAL_UINT32(1, st.levels);
}

void test_coarser_level_starts_above_threshold_and_is_exact() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write(kW1, weekFile(0, 301));
    WeeklySummary::LadderStats st;
    TEST_ASSERT_EQUAL(0, static_cast<int>(WeeklySummary::updateLadder(sd, st)));
    TEST_ASSERT_EQUAL_UINT32(2, st.levels);
    TEST_ASSERT_EQUAL_UINT32(150, st.records_written); // week 300 waits for week 301
    const auto w2 = readLevel(sd, kW2);
    TEST_ASSERT_EQUAL_UINT32(150, w2.size());
    const QuarterHourRow::SpanRow a = week(6), b = week(7);
    TEST_ASSERT_EQUAL_UINT32(a.start_epoch, w2[3].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(b.end_epoch, w2[3].end_epoch);
    TEST_ASSERT_EQUAL_UINT32(a.metrics[TEMP].count + b.metrics[TEMP].count, w2[3].metrics[TEMP].count);
    TEST_ASSERT_TRUE(a.metrics[TEMP].sum + b.metrics[TEMP].sum == w2[3].metrics[TEMP].sum);
    TEST_ASSERT_TRUE(w2[3].metrics[TEMP].min == 10.0f); // week 7 (7 % 7 == 0)
    TEST_ASSERT_EQUAL_UINT32(b.metrics[TEMP].min_epoch, w2[3].metrics[TEMP].min_epoch);
    TEST_ASSERT_EQUAL_UINT32(a.metrics[TEMP].max_epoch, w2[3].metrics[TEMP].max_epoch);
    TEST_ASSERT_FALSE(g_root->exists(kW4));

    // Nothing new: nothing written. One more week completes the pending bucket.
    TEST_ASSERT_EQUAL(0, static_cast<int>(WeeklySummary::updateLadder(sd, st)));
    TEST_ASSERT_EQUAL_UINT32(0, st.records_written);
    g_root->write(kW1, weekFile(0, 302));
    TEST_ASSERT_EQUAL(0, static_cast<int>(WeeklySummary::updateLadder(sd, st)));
    TEST_ASSERT_EQUAL_UINT32(1, st.records_written);
    TEST_ASSERT_EQUAL_UINT32(151, readLevel(sd, kW2).size());
}

void test_coarser_level_survives_loss_of_finer_rows() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write(kW1, weekFile(0, 302));
    WeeklySummary::LadderStats st;
    TEST_ASSERT_EQUAL(0, static_cast<int>(WeeklySummary::updateLadder(sd, st)));
    // w2 exists now, so it keeps updating even though w1 stays at 302 records.
    g_root->write(kW1, weekFile(0, 304));
    TEST_ASSERT_EQUAL(0, static_cast<int>(WeeklySummary::updateLadder(sd, st)));
    TEST_ASSERT_EQUAL_UINT32(152, readLevel(sd, kW2).size());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_bucket_bounds_are_anchored_at_monday);
    RUN_TEST(test_weekly_writer_from_quarter_rows);
    RUN_TEST(test_no_coarser_level_at_or_below_the_threshold);
    RUN_TEST(test_coarser_level_starts_above_threshold_and_is_exact);
    RUN_TEST(test_coarser_level_survives_loss_of_finer_rows);
    return UNITY_END();
}
