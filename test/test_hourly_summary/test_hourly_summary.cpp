// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string>
#include <vector>

#include "ArduinoMock.h"
#include "SdTempRoot.h"
#include "core/Logger.h"
#include "core/WakePowerGuard.h"
#include "modules/HistoryBucket.h"
#include "modules/HourlySummary.h"
#include "modules/LongHistoryFiles.h"
#include "modules/QuarterHourLogger.h"
#include "modules/SdCardManager.h"
#include "modules/SummaryWriter.h"

namespace {

constexpr uint32_t kOct31 = 1793404800; // 2026-10-31T00:00:00Z
constexpr uint32_t kNov1 = 1793491200;  // 2026-11-01T00:00:00Z
constexpr uint32_t kQ = QuarterHourRow::kQuarterSeconds;
constexpr uint32_t kH = HourlySummary::kHourSeconds;
constexpr const char *kNovFile = "/aura/history/hourly/2026-11.csv";
constexpr const char *kOctFile = "/aura/history/hourly/2026-10.csv";
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;

SdTempRoot *g_root = nullptr;
const HourlySummary::Level kLevel;

QuarterHourRow::Row quarter(uint32_t start, float value) {
    QuarterHourRow::Row r;
    r.start_epoch = start;
    HistoryAggregate &a = r.metrics[TEMP];
    a.count = 30;
    a.sum = 30.0 * value + 0.125;
    a.min = value - 1.0f;
    a.max = value + 1.0f;
    a.min_epoch = start + 17;
    a.max_epoch = start + 800;
    return r;
}

std::string quarterFile(const std::vector<QuarterHourRow::Row> &rows) {
    String text;
    QuarterHourRow::appendHeader(text);
    for (const auto &r : rows) TEST_ASSERT_EQUAL(0, static_cast<int>(QuarterHourRow::appendRow(r, text)));
    return text;
}

std::vector<QuarterHourRow::SpanRow> g_read;
bool collect(const QuarterHourRow::SpanRow &row, size_t, void *) {
    g_read.push_back(row);
    return true;
}

std::vector<QuarterHourRow::SpanRow> readHourly(SdCardManager &sd, const char *path) {
    g_read.clear();
    LongHistoryFiles::ReadStats st;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistoryFiles::ReadResult::Read),
                      static_cast<int>(LongHistoryFiles::readSpanFile(sd, path, &collect, nullptr, st)));
    TEST_ASSERT_EQUAL_UINT32(0, st.skipped);
    return g_read;
}

void assertSameRecord(const QuarterHourRow::SpanRow &a, const QuarterHourRow::SpanRow &b) {
    TEST_ASSERT_EQUAL_UINT32(a.start_epoch, b.start_epoch);
    TEST_ASSERT_EQUAL_UINT32(a.end_epoch, b.end_epoch);
    for (int m = 0; m < ChartsHistory::kMetricCount; ++m) {
        TEST_ASSERT_EQUAL_UINT32(a.metrics[m].count, b.metrics[m].count);
        TEST_ASSERT_TRUE(a.metrics[m].sum == b.metrics[m].sum);
        TEST_ASSERT_TRUE(a.metrics[m].min == b.metrics[m].min);
        TEST_ASSERT_TRUE(a.metrics[m].max == b.metrics[m].max);
        TEST_ASSERT_EQUAL_UINT32(a.metrics[m].min_epoch, b.metrics[m].min_epoch);
        TEST_ASSERT_EQUAL_UINT32(a.metrics[m].max_epoch, b.metrics[m].max_epoch);
    }
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

void test_span_rows_round_trip_and_layouts_do_not_mix() {
    QuarterHourRow::SpanRow span;
    span.start_epoch = kNov1;
    span.end_epoch = kNov1 + 92 * kQ; // a 23-hour local day
    span.metrics[TEMP] = quarter(kNov1 + 80 * kQ, 21.5f).metrics[TEMP];
    String text;
    TEST_ASSERT_EQUAL(0, static_cast<int>(QuarterHourRow::appendSpanRow(span, text)));
    QuarterHourRow::SpanRow back;
    TEST_ASSERT_EQUAL(0, static_cast<int>(QuarterHourRow::decodeSpanRow(text.c_str(), text.length() - 1, back)));
    assertSameRecord(span, back);
    String qh, sh;
    QuarterHourRow::appendHeader(qh);
    QuarterHourRow::appendSpanHeader(sh);
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourRow::DecodeResult::BadHeader),
                      static_cast<int>(QuarterHourRow::decodeHeader(sh.c_str(), sh.length() - 1)));
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourRow::DecodeResult::BadHeader),
                      static_cast<int>(QuarterHourRow::decodeSpanHeader(qh.c_str(), qh.length() - 1)));
    QuarterHourRow::SpanRow reversed = span;
    reversed.end_epoch = span.start_epoch;
    String unchanged;
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourRow::EncodeResult::BadStart),
                      static_cast<int>(QuarterHourRow::appendSpanRow(reversed, unchanged)));
    TEST_ASSERT_EQUAL_UINT32(0, unchanged.length());
}

void test_live_hour_from_logged_quarters() {
    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    SummaryWriter hourly(sd, kLevel);
    TEST_ASSERT_TRUE(logger.addRowListener(&SummaryWriter::rowThunk, &hourly));
    logger.setEnabled(true);
    hourly.setEnabled(true);
    logger.poll(static_cast<time_t>(kNov1), true);
    hourly.poll(static_cast<time_t>(kNov1), true, logger.prepared());
    TEST_ASSERT_EQUAL(static_cast<int>(SummaryWriter::State::Recording), static_cast<int>(hourly.status().state));

    for (uint32_t i = 0; i < 12; ++i) { // 3 quarters-of-5-min x 4 = one hour
        ChartsHistory::CompletedInterval iv;
        iv.entry.start_epoch = kNov1 + i * ChartsHistory::kStepSeconds;
        iv.entry.kind = ChartsHistory::Summary;
        iv.entry.valid_mask = ChartsHistory::metricBit(ChartsHistory::METRIC_TEMPERATURE);
        iv.entry.samples[TEMP] = 10;
        iv.entry.minimum[TEMP] = 20.0f + i;
        iv.entry.maximum[TEMP] = 20.0f + i;
        iv.sums[TEMP] = 10.0 * (20.0 + i);
        logger.onInterval(iv);
    }
    const auto rows = readHourly(sd, kNovFile);
    TEST_ASSERT_EQUAL_UINT32(1, rows.size());
    TEST_ASSERT_EQUAL_UINT32(kNov1, rows[0].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(kNov1 + kH, rows[0].end_epoch);
    TEST_ASSERT_EQUAL_UINT32(120, rows[0].metrics[TEMP].count);
    TEST_ASSERT_TRUE(rows[0].metrics[TEMP].min == 20.0f);
    TEST_ASSERT_EQUAL_UINT32(kNov1, rows[0].metrics[TEMP].min_epoch);
    TEST_ASSERT_TRUE(rows[0].metrics[TEMP].max == 31.0f);
    TEST_ASSERT_EQUAL_UINT32(kNov1 + 11 * ChartsHistory::kStepSeconds, rows[0].metrics[TEMP].max_epoch);
    TEST_ASSERT_EQUAL_UINT32(1, hourly.status().records_written);
}

void test_boot_rebuild_equals_live_and_continues_open_hour() {
    SdCardManager sd;
    g_root->mount(sd);
    // 15-minute rows for 00:00-02:30 on Nov 1; no hourly file yet.
    std::vector<QuarterHourRow::Row> rows;
    for (uint32_t q = 0; q < 11; ++q) rows.push_back(quarter(kNov1 + q * kQ, 20.0f + q));
    g_root->write("/aura/history/15min/2026/11/2026-11-01.csv", quarterFile(rows));

    QuarterHourLogger logger(sd);
    SummaryWriter hourly(sd, kLevel);
    TEST_ASSERT_TRUE(logger.addRowListener(&SummaryWriter::rowThunk, &hourly));
    logger.setEnabled(true);
    hourly.setEnabled(true);
    logger.poll(static_cast<time_t>(kNov1 + 11 * kQ), true);
    hourly.poll(static_cast<time_t>(kNov1 + 11 * kQ), true, logger.prepared());
    TEST_ASSERT_EQUAL_UINT32(2, hourly.status().records_rebuilt);
    const auto got = readHourly(sd, kNovFile);
    TEST_ASSERT_EQUAL_UINT32(2, got.size());
    for (uint32_t h = 0; h < 2; ++h) {
        HistoryBucket::Record expected;
        HistoryBucket::Stats st;
        TEST_ASSERT_TRUE(HistoryBucket::deriveBucket(rows.data(), rows.size(), kNov1 + h * kH,
                                                     kNov1 + (h + 1) * kH, expected, st));
        assertSameRecord(expected, got[h]);
    }
    // 02:00-02:45 is open with its three quarters on the card.
    TEST_ASSERT_TRUE(hourly.hasOpenBucket());
    TEST_ASSERT_EQUAL_UINT32(3, hourly.openRecord().source_records);
    // The fourth quarter arrives live and closes the hour.
    hourly.onRow(quarter(kNov1 + 11 * kQ, 31.0f));
    TEST_ASSERT_EQUAL_UINT32(3, readHourly(sd, kNovFile).size());
    TEST_ASSERT_EQUAL_UINT32(4 * 30, g_read[2].metrics[TEMP].count);
}

void test_damaged_hourly_file_is_rebuilt() {
    SdCardManager sd;
    g_root->mount(sd);
    std::vector<QuarterHourRow::Row> rows;
    for (uint32_t q = 0; q < 8; ++q) rows.push_back(quarter(kNov1 + q * kQ, 20.0f));
    g_root->write("/aura/history/15min/2026/11/2026-11-01.csv", quarterFile(rows));
    g_root->write(kNovFile, "garbage,header\n1,2\n");
    QuarterHourLogger logger(sd);
    SummaryWriter hourly(sd, kLevel);
    logger.setEnabled(true);
    hourly.setEnabled(true);
    logger.poll(static_cast<time_t>(kNov1 + 9 * kQ), true);
    hourly.poll(static_cast<time_t>(kNov1 + 9 * kQ), true, logger.prepared());
    TEST_ASSERT_EQUAL_UINT32(1, hourly.status().files_rebuilt);
    TEST_ASSERT_EQUAL_UINT32(2, readHourly(sd, kNovFile).size());
}

void test_poll_closes_an_hour_whose_last_quarter_is_missing() {
    SdCardManager sd;
    g_root->mount(sd);
    QuarterHourLogger logger(sd);
    SummaryWriter hourly(sd, kLevel);
    logger.setEnabled(true);
    hourly.setEnabled(true);
    logger.poll(static_cast<time_t>(kNov1), true);
    hourly.poll(static_cast<time_t>(kNov1), true, logger.prepared());
    hourly.onRow(quarter(kNov1, 20.0f));
    hourly.poll(static_cast<time_t>(kNov1 + kH + SummaryWriter::kCloseDelaySeconds - 1), true, true);
    TEST_ASSERT_FALSE(g_root->exists(kNovFile));
    hourly.poll(static_cast<time_t>(kNov1 + kH + SummaryWriter::kCloseDelaySeconds), true, true);
    TEST_ASSERT_EQUAL_UINT32(1, readHourly(sd, kNovFile).size());
    // A late quarter of that hour is refused.
    hourly.onRow(quarter(kNov1 + 3 * kQ, 20.0f));
    TEST_ASSERT_EQUAL_UINT32(1, readHourly(sd, kNovFile).size());
    TEST_ASSERT_EQUAL_UINT32(1, hourly.status().refused);
}

void test_range_reads_across_two_utc_month_files() {
    SdCardManager sd;
    g_root->mount(sd);
    // Live writer across the UTC month boundary: 22:00Z Oct 31 to 02:00Z Nov 1.
    QuarterHourLogger logger(sd);
    SummaryWriter hourly(sd, kLevel);
    logger.setEnabled(true);
    hourly.setEnabled(true);
    logger.poll(static_cast<time_t>(kOct31), true);
    hourly.poll(static_cast<time_t>(kOct31), true, logger.prepared());
    for (uint32_t t = kOct31 + 22 * kH; t < kNov1 + 2 * kH; t += kQ) hourly.onRow(quarter(t, 20.0f));
    TEST_ASSERT_EQUAL_UINT32(2, readHourly(sd, kOctFile).size());
    TEST_ASSERT_EQUAL_UINT32(2, readHourly(sd, kNovFile).size());
    // A local (New York, UTC-4) day of Oct 31 ends at 04:00Z Nov 1.
    g_read.clear();
    HourlySummary::ReadStats st;
    TEST_ASSERT_EQUAL(static_cast<int>(HourlySummary::ReadResult::Read),
                      static_cast<int>(HourlySummary::readRange(sd, kOct31 + 4 * kH, kNov1 + 4 * kH, &collect,
                                                                nullptr, st)));
    TEST_ASSERT_EQUAL_UINT32(4, st.records);
    TEST_ASSERT_EQUAL_UINT32(2, st.files_read);
    TEST_ASSERT_EQUAL_UINT32(kOct31 + 22 * kH, g_read[0].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(kNov1 + kH, g_read[3].start_epoch);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_span_rows_round_trip_and_layouts_do_not_mix);
    RUN_TEST(test_live_hour_from_logged_quarters);
    RUN_TEST(test_boot_rebuild_equals_live_and_continues_open_hour);
    RUN_TEST(test_damaged_hourly_file_is_rebuilt);
    RUN_TEST(test_poll_closes_an_hour_whose_last_quarter_is_missing);
    RUN_TEST(test_range_reads_across_two_utc_month_files);
    return UNITY_END();
}
