// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <stdlib.h>
#include <time.h>

#include <string>
#include <vector>

#include "ArduinoMock.h"
#include "SdTempRoot.h"
#include "core/Logger.h"
#include "core/WakePowerGuard.h"
#include "modules/DailySummary.h"
#include "modules/HistoryBucket.h"
#include "modules/LongHistoryFiles.h"
#include "modules/QuarterHourLogger.h"
#include "modules/SdCardManager.h"
#include "modules/SummaryWriter.h"

namespace {

constexpr uint32_t kMar8 = 1772928000; // 2026-03-08T00:00:00Z (US DST starts that day)
constexpr uint32_t kNov1 = 1793491200; // 2026-11-01T00:00:00Z (US DST ends that day)
constexpr uint32_t kNov3 = 1793664000; // 2026-11-03T00:00:00Z
constexpr uint32_t kQ = QuarterHourRow::kQuarterSeconds;
constexpr uint32_t kH = 3600;
constexpr const char *kNewYork = "EST5EDT,M3.2.0,M11.1.0";
constexpr const char *kFile2026 = "/aura/history/days/2026.csv";
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;

SdTempRoot *g_root = nullptr;
const DailySummary::Level kLevel;

void useZone(const char *tz) {
    setenv("TZ", tz, 1);
    tzset();
}

QuarterHourRow::Row quarter(uint32_t start, float value) {
    QuarterHourRow::Row r;
    r.start_epoch = start;
    HistoryAggregate &a = r.metrics[TEMP];
    a.count = 30;
    a.sum = 30.0 * value;
    a.min = value - 1.0f;
    a.max = value + 1.0f;
    a.min_epoch = start + 5;
    a.max_epoch = start + 600;
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

std::vector<QuarterHourRow::SpanRow> readDays(SdCardManager &sd, uint32_t year) {
    g_read.clear();
    DailySummary::ReadStats st;
    TEST_ASSERT_EQUAL(static_cast<int>(DailySummary::ReadResult::Read),
                      static_cast<int>(DailySummary::readYear(sd, year, &collect, nullptr, st)));
    TEST_ASSERT_EQUAL_UINT32(0, st.skipped);
    return g_read;
}

struct Rig {
    SdCardManager sd;
    QuarterHourLogger logger{sd};
    SummaryWriter daily{sd, kLevel};
    void start(uint32_t now) {
        g_root->mount(sd);
        TEST_ASSERT_TRUE(logger.addRowListener(&SummaryWriter::rowThunk, &daily));
        logger.setEnabled(true);
        daily.setEnabled(true);
        logger.poll(static_cast<time_t>(now), true);
        daily.poll(static_cast<time_t>(now), true, logger.prepared());
        TEST_ASSERT_EQUAL(static_cast<int>(SummaryWriter::State::Recording),
                          static_cast<int>(daily.status().state));
    }
    void feed(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t < to; t += kQ) daily.onRow(quarter(t, 20.0f));
    }
};

} // namespace

void setUp() {
    setMillis(0);
    WakePowerGuard::resetForTest();
    Logger::resetRecentForTest();
    g_root = new SdTempRoot();
    useZone("UTC0");
}

void tearDown() {
    delete g_root;
    g_root = nullptr;
    WakePowerGuard::resetForTest();
    useZone("UTC0");
}

void test_local_day_bounds_and_file() {
    useZone(kNewYork);
    SummaryLevel::Bucket b;
    // 03:00Z Nov 3 is 22:00 EST Nov 2.
    TEST_ASSERT_TRUE(kLevel.bucketFor(kNov3 + 3 * kH, b));
    TEST_ASSERT_EQUAL_UINT32(kNov3 - 19 * kH, b.start); // 05:00Z Nov 2
    TEST_ASSERT_EQUAL_UINT32(kNov3 + 5 * kH, b.end);
    TEST_ASSERT_EQUAL_STRING(kFile2026, b.path);
    // DST days are 23 and 25 hours long: 92 and 100 quarters.
    TEST_ASSERT_TRUE(kLevel.bucketFor(kMar8 + 12 * kH, b));
    TEST_ASSERT_EQUAL_UINT32(92, (b.end - b.start) / kQ);
    TEST_ASSERT_EQUAL_UINT32(kMar8 + 5 * kH, b.start);
    TEST_ASSERT_TRUE(kLevel.bucketFor(kNov1 + 12 * kH, b));
    TEST_ASSERT_EQUAL_UINT32(100, (b.end - b.start) / kQ);
    TEST_ASSERT_EQUAL_UINT32(kNov1 + 4 * kH, b.start);
}

void test_dst_day_is_one_exact_record_of_100_quarters() {
    useZone(kNewYork);
    Rig rig;
    rig.start(kNov1);
    rig.feed(kNov1 + 4 * kH, kNov1 + 29 * kH);
    const auto days = readDays(rig.sd, 2026);
    TEST_ASSERT_EQUAL_UINT32(1, days.size());
    TEST_ASSERT_EQUAL_UINT32(kNov1 + 4 * kH, days[0].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(kNov1 + 29 * kH, days[0].end_epoch);
    TEST_ASSERT_EQUAL_UINT32(100 * 30, days[0].metrics[TEMP].count);
    TEST_ASSERT_TRUE(days[0].metrics[TEMP].sum == 100 * 30 * 20.0);
    TEST_ASSERT_EQUAL_UINT32(kNov1 + 4 * kH + 5, days[0].metrics[TEMP].min_epoch);
}

void test_zone_change_mid_day_ends_day_at_new_midnight() {
    Rig rig;
    rig.start(kNov3);
    rig.feed(kNov3, kNov3 + 8 * kH); // UTC day open
    useZone(kNewYork);               // 08:00Z = 03:00 EST Nov 3
    rig.feed(kNov3 + 8 * kH, kNov3 + 54 * kH);
    const auto days = readDays(rig.sd, 2026);
    TEST_ASSERT_EQUAL_UINT32(2, days.size());
    // Finished under UTC's start, ended at the new zone's midnight (05:00Z Nov 4).
    TEST_ASSERT_EQUAL_UINT32(kNov3, days[0].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(kNov3 + 29 * kH, days[0].end_epoch);
    TEST_ASSERT_EQUAL_UINT32(116 * 30, days[0].metrics[TEMP].count);
    TEST_ASSERT_EQUAL_UINT32(kNov3 + 29 * kH, days[1].start_epoch);
    TEST_ASSERT_EQUAL_UINT32(kNov3 + 53 * kH, days[1].end_epoch);
}

void test_zone_change_between_days_never_overlaps_finished_day() {
    Rig rig;
    rig.start(kNov3);
    rig.feed(kNov3, kNov3 + 24 * kH + kQ); // closes the UTC day of Nov 3
    useZone("CET-1");                      // local day of Nov 4 began 23:00Z Nov 3
    rig.feed(kNov3 + 24 * kH + kQ, kNov3 + 48 * kH);
    const auto days = readDays(rig.sd, 2026);
    TEST_ASSERT_EQUAL_UINT32(2, days.size());
    TEST_ASSERT_EQUAL_UINT32(kNov3 + 24 * kH, days[0].end_epoch);
    TEST_ASSERT_EQUAL_UINT32(kNov3 + 24 * kH, days[1].start_epoch); // starts where day 1 ended
    TEST_ASSERT_EQUAL_UINT32(kNov3 + 47 * kH, days[1].end_epoch);
}

void test_boot_rebuild_matches_derivation() {
    useZone(kNewYork);
    std::vector<QuarterHourRow::Row> rows;
    for (uint32_t t = kNov3; t < kNov3 + 24 * kH; t += kQ) rows.push_back(quarter(t, 18.0f));
    g_root->write("/aura/history/15min/2026/11/2026-11-03.csv", quarterFile(rows));
    Rig rig;
    rig.start(kNov3 + 24 * kH);
    // 00:00-05:00Z Nov 3 closes local Nov 2; 05:00Z onward is local Nov 3 (open).
    TEST_ASSERT_EQUAL_UINT32(1, rig.daily.status().records_rebuilt);
    const auto days = readDays(rig.sd, 2026);
    TEST_ASSERT_EQUAL_UINT32(1, days.size());
    HistoryBucket::Record expected;
    HistoryBucket::Stats st;
    TEST_ASSERT_TRUE(HistoryBucket::deriveBucket(rows.data(), rows.size(), kNov3 - 19 * kH, kNov3 + 5 * kH,
                                                 expected, st));
    TEST_ASSERT_EQUAL_UINT32(expected.metrics[TEMP].count, days[0].metrics[TEMP].count);
    TEST_ASSERT_TRUE(expected.metrics[TEMP].sum == days[0].metrics[TEMP].sum);
    TEST_ASSERT_EQUAL_UINT32(kNov3 - 19 * kH, days[0].start_epoch);
    TEST_ASSERT_TRUE(rig.daily.hasOpenBucket());
    TEST_ASSERT_EQUAL_UINT32(76, rig.daily.openRecord().source_records);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_local_day_bounds_and_file);
    RUN_TEST(test_dst_day_is_one_exact_record_of_100_quarters);
    RUN_TEST(test_zone_change_mid_day_ends_day_at_new_midnight);
    RUN_TEST(test_zone_change_between_days_never_overlaps_finished_day);
    RUN_TEST(test_boot_rebuild_matches_derivation);
    return UNITY_END();
}
