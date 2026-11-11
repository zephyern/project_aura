// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <stdlib.h>
#include <time.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ArduinoMock.h"
#include "SdTempRoot.h"
#include "core/Logger.h"
#include "core/WakePowerGuard.h"
#include "core/LocalDate.h"
#include "modules/ChartsHistory.h"
#include "modules/DailyExtremaHistory.h"
#include "modules/LongHistory.h"
#include "modules/LongHistoryFiles.h"
#include "modules/SdCardManager.h"

namespace {

constexpr uint32_t kNov8 = 1794096000;  // 2026-11-08T00:00:00Z
constexpr uint32_t kNov9 = 1794182400;  // 2026-11-09T00:00:00Z
constexpr uint32_t kNov10 = 1794268800; // 2026-11-10T00:00:00Z
constexpr uint32_t kQ = QuarterHourRow::kQuarterSeconds;
constexpr uint32_t kH = 3600;
constexpr const char *kNewYork = "EST5EDT,M3.2.0,M11.1.0";
constexpr const char *kDays = "/aura/history/days/2026.csv";
constexpr const char *kQ8 = "/aura/history/15min/2026/11/2026-11-08.csv";
constexpr const char *kQ9 = "/aura/history/15min/2026/11/2026-11-09.csv";
constexpr const char *kQ10 = "/aura/history/15min/2026/11/2026-11-10.csv";
constexpr const char *kHourly = "/aura/history/hourly/2026-11.csv";
constexpr const char *kHeader = "date,metric,unit,min,min_time,max,max_time,sample_count\n";
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;
constexpr int PRESSURE = ChartsHistory::METRIC_PRESSURE;

SdTempRoot *g_root = nullptr;

QuarterHourRow::Row quarter(uint32_t start) {
    QuarterHourRow::Row r;
    r.start_epoch = start;
    HistoryAggregate &a = r.metrics[TEMP];
    a.count = 3;
    a.sum = 60.0;
    a.min = 20.0f;
    a.max = 20.0f;
    a.min_epoch = start;
    a.max_epoch = start;
    return r;
}

// 15-minute files with a row every quarter in [from, to).
void writeQuarters(uint32_t from, uint32_t to) {
    for (uint32_t day = from - from % 86400; day < to; day += 86400) {
        String text;
        QuarterHourRow::appendHeader(text);
        for (uint32_t t = std::max(day, from); t < std::min(day + 86400, to); t += kQ) {
            TEST_ASSERT_EQUAL(0, static_cast<int>(QuarterHourRow::appendRow(quarter(t), text)));
        }
        uint32_t key = 0;
        char path[LongHistoryFiles::kPathCapacity];
        TEST_ASSERT_TRUE(LocalDate::utcDayKey(static_cast<time_t>(day), key));
        TEST_ASSERT_TRUE(LongHistoryFiles::quarterFilePath(key, path));
        g_root->write(path, text.c_str());
    }
}

std::vector<QuarterHourRow::Row> g_rows;
bool collectRow(const QuarterHourRow::Row &row, size_t, void *) {
    g_rows.push_back(row);
    return true;
}
std::vector<QuarterHourRow::Row> quarters(SdCardManager &sd, const char *path) {
    g_rows.clear();
    LongHistoryFiles::ReadStats st;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistoryFiles::ReadResult::Read),
                      static_cast<int>(LongHistoryFiles::readQuarterFile(sd, path, &collectRow, nullptr, st)));
    return g_rows;
}
std::vector<QuarterHourRow::SpanRow> g_spans;
bool collectSpan(const QuarterHourRow::SpanRow &row, size_t, void *) {
    g_spans.push_back(row);
    return true;
}
std::vector<QuarterHourRow::SpanRow> spans(SdCardManager &sd, const char *path) {
    g_spans.clear();
    LongHistoryFiles::ReadStats st;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistoryFiles::ReadResult::Read),
                      static_cast<int>(LongHistoryFiles::readSpanFile(sd, path, &collectSpan, nullptr, st)));
    return g_spans;
}

bool collectText(const char *text, size_t len, void *context) {
    static_cast<std::string *>(context)->append(text, len);
    return true;
}

struct Rig {
    SdCardManager sd;
    ChartsHistory charts;
    LongHistory history{sd};
    Rig() {
        g_root->mount(sd);
        TEST_ASSERT_TRUE(history.attach(charts));
        history.setEnabled(true);
    }
};

const LongHistory::OpResult *find(const LongHistory::OpResults &r, const char *path) {
    for (const auto &item : r.items) {
        if (item.path == path) return &item;
    }
    return nullptr;
}

} // namespace

void setUp() {
    setMillis(0);
    WakePowerGuard::resetForTest();
    Logger::resetRecentForTest();
    setenv("TZ", kNewYork, 1);
    tzset();
    g_root = new SdTempRoot();
}

void tearDown() {
    delete g_root;
    g_root = nullptr;
    WakePowerGuard::resetForTest();
}

void test_daily_csv_uses_the_upstream_format() {
    // One local day (Nov 9 in New York: 05:00Z to 05:00Z next day).
    QuarterHourRow::SpanRow day;
    day.start_epoch = kNov9 + 5 * kH;
    day.end_epoch = kNov10 + 5 * kH;
    HistoryAggregate &t = day.metrics[TEMP];
    t.count = 288;
    t.sum = 288.0 * 20.0;
    t.min = 19.5f;
    t.max = 21.5f;
    t.min_epoch = kNov9 + 11 * kH + 7;  // 06:00:07 local
    t.max_epoch = kNov9 + 20 * kH + 30; // 15:00:30 local
    HistoryAggregate &p = day.metrics[PRESSURE];
    p.count = 10;
    p.sum = 10132.0;
    p.min = 1013.2f;
    p.max = 1020.0f;
    p.min_epoch = kNov9 + 6 * kH;
    p.max_epoch = kNov9 + 7 * kH;
    String text;
    QuarterHourRow::appendSpanHeader(text);
    TEST_ASSERT_EQUAL(0, static_cast<int>(QuarterHourRow::appendSpanRow(day, text)));
    g_root->write(kDays, text.c_str());

    Rig rig;
    std::string c, f;
    size_t bytes = 0;
    uint32_t last_end = 0;
    TEST_ASSERT_EQUAL(0, static_cast<int>(rig.history.dailyCsv(true, UINT32_MAX, &collectText, &c, bytes, last_end)));
    TEST_ASSERT_EQUAL_STRING((std::string(kHeader) +
                              "2026-11-09,temperature,C,19.5,06:00:07,21.5,15:00:30,288\n"
                              "2026-11-09,pressure,hPa,1013.2,01:00:00,1020.0,02:00:00,10\n")
                                 .c_str(),
                             c.c_str());
    TEST_ASSERT_EQUAL_UINT32(c.size(), bytes);
    TEST_ASSERT_EQUAL_UINT32(day.end_epoch, last_end);
    TEST_ASSERT_EQUAL_STRING(kHeader, DailyExtremaHistory::csvHeader());
    TEST_ASSERT_EQUAL(0, static_cast<int>(rig.history.dailyCsv(false, UINT32_MAX, &collectText, &f, bytes, last_end)));
    TEST_ASSERT_EQUAL_STRING((std::string(kHeader) +
                              "2026-11-09,temperature,F,67.1,06:00:07,70.7,15:00:30,288\n"
                              "2026-11-09,pressure,inHg,29.92,01:00:00,30.12,02:00:00,10\n")
                                 .c_str(),
                             f.c_str());
}

void test_second_pass_with_the_limit_matches_the_first() {
    writeQuarters(kNov8, kNov10 + 6 * kH);
    Rig rig;
    rig.history.poll(static_cast<time_t>(kNov10 + 6 * kH), true, LongHistory::Space{}); // rebuilds Nov 7 (partial) and Nov 8, Nov 9
    size_t first_bytes = 0, second_bytes = 0;
    uint32_t limit = 0, end = 0;
    std::string first, second;
    TEST_ASSERT_EQUAL(0, static_cast<int>(rig.history.dailyCsv(true, UINT32_MAX, &collectText, &first,
                                                               first_bytes, limit)));
    TEST_ASSERT_EQUAL_UINT32(kNov10 + 5 * kH, limit);
    // A day closing between the passes does not change the second pass.
    QuarterHourRow::SpanRow later;
    later.start_epoch = limit;
    later.end_epoch = limit + 24 * kH;
    later.metrics[TEMP] = quarter(limit).metrics[TEMP];
    String extra;
    TEST_ASSERT_EQUAL(0, static_cast<int>(QuarterHourRow::appendSpanRow(later, extra)));
    g_root->write(kDays, g_root->read(kDays) + extra.c_str());
    TEST_ASSERT_EQUAL(0, static_cast<int>(rig.history.dailyCsv(true, limit, &collectText, &second,
                                                               second_bytes, end)));
    TEST_ASSERT_EQUAL_STRING(first.c_str(), second.c_str());
    TEST_ASSERT_EQUAL_UINT32(first_bytes, second_bytes);
}

void test_clear_all_removes_every_group_and_the_old_daily_file() {
    writeQuarters(kNov8, kNov10 + 6 * kH);
    g_root->write("/aura/history/daily.csv", kHeader);
    g_root->write("/aura/history/current_day.a.bin", "x");
    Rig rig;
    rig.history.poll(static_cast<time_t>(kNov10 + 6 * kH), true, LongHistory::Space{});
    TEST_ASSERT_TRUE(g_root->exists(kHourly));
    TEST_ASSERT_TRUE(g_root->exists(kDays));
    char first[16] = {};
    TEST_ASSERT_TRUE(rig.history.firstDay(first, sizeof(first)));
    TEST_ASSERT_EQUAL_STRING("2026-11-07", first);

    LongHistory::OpResults r;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::OpStatus::Done), static_cast<int>(rig.history.clearAll(r)));
    TEST_ASSERT_TRUE(r.ok());
    const char *groups[] = {"/aura/history/15min", "/aura/history/hourly", "/aura/history/days",
                            "/aura/history/weeks"};
    for (const char *g : groups) {
        const LongHistory::OpResult *item = find(r, g);
        TEST_ASSERT_NOT_NULL(item);
        TEST_ASSERT_TRUE(item->ok);
        TEST_ASSERT_FALSE(g_root->exists(g));
    }
    TEST_ASSERT_TRUE(find(r, "/aura/history/daily.csv")->removed);
    TEST_ASSERT_TRUE(find(r, "/aura/history/current_day.a.bin")->removed);
    TEST_ASSERT_FALSE(find(r, "/aura/history/current_day.b.bin")->existed);
    TEST_ASSERT_TRUE(find(r, "/aura/history/current_day.b.bin")->ok);
    TEST_ASSERT_FALSE(g_root->exists("/aura/history/daily.csv"));
    TEST_ASSERT_FALSE(rig.history.firstDay(first, sizeof(first)));
}

void test_failed_removal_is_reported() {
    g_root->write("/aura/history/15min", "x"); // a file where the directory is expected
    Rig rig;
    LongHistory::OpResults r;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::OpStatus::Done), static_cast<int>(rig.history.clearAll(r)));
    TEST_ASSERT_FALSE(r.ok());
    TEST_ASSERT_FALSE(find(r, "/aura/history/15min")->ok);
    TEST_ASSERT_TRUE(find(r, "/aura/history/hourly")->ok); // the other groups are still attempted
}

void test_clear_today_cuts_only_today_across_two_utc_files() {
    // Now: Nov 10 02:00Z = Nov 9 21:00 in New York; today began Nov 9 05:00Z.
    const uint32_t now = kNov10 + 2 * kH;
    const uint32_t midnight = kNov9 + 5 * kH;
    writeQuarters(kNov8, now);
    Rig rig;
    rig.history.poll(static_cast<time_t>(now), true, LongHistory::Space{});
    const std::string before8 = g_root->read(kQ8);

    LongHistory::OpResults r;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::OpStatus::Done),
                      static_cast<int>(rig.history.clearToday(static_cast<time_t>(now), r)));
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_TRUE(find(r, kQ9)->removed);
    TEST_ASSERT_TRUE(find(r, kQ10)->removed);
    TEST_ASSERT_NULL(find(r, kQ8)); // not touched, not listed
    TEST_ASSERT_EQUAL_STRING(before8.c_str(), g_root->read(kQ8).c_str());
    const auto nov9 = quarters(rig.sd, kQ9);
    TEST_ASSERT_EQUAL_UINT32(5 * 4, nov9.size());
    TEST_ASSERT_EQUAL_UINT32(midnight - kQ, nov9.back().start_epoch);
    TEST_ASSERT_EQUAL_UINT32(0, quarters(rig.sd, kQ10).size());

    // Summaries keep everything before midnight and are rebuilt up to it.
    rig.history.poll(static_cast<time_t>(now), true, LongHistory::Space{});
    const auto hours = spans(rig.sd, kHourly);
    TEST_ASSERT_EQUAL_UINT32(midnight, hours.back().end_epoch);
    TEST_ASSERT_EQUAL_UINT32(kNov8, hours.front().start_epoch);
    const auto days = spans(rig.sd, kDays);
    TEST_ASSERT_EQUAL_UINT32(midnight, days.back().end_epoch);
    uint32_t key = 0, samples = 0;
    TEST_ASSERT_FALSE(rig.history.currentDay(key, samples)); // today is empty again
}

std::vector<QuarterHourRow::SpanRow> g_chart;
void collectChart(const QuarterHourRow::SpanRow &row, void *) { g_chart.push_back(row); }

void test_hourly_chart_reads_the_range_and_the_hour_in_progress() {
    writeQuarters(kNov8, kNov10 + 6 * kH);
    Rig rig;
    rig.history.poll(static_cast<time_t>(kNov10 + 6 * kH), true, LongHistory::Space{});
    g_chart.clear();
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::ChartRead::Read),
                      static_cast<int>(rig.history.hourlyChart(kNov9, kNov10, &collectChart, nullptr)));
    TEST_ASSERT_EQUAL_UINT32(24, g_chart.size());
    for (size_t i = 0; i < g_chart.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32(kNov9 + i * kH, g_chart[i].start_epoch);
        TEST_ASSERT_EQUAL_UINT32(12, g_chart[i].metrics[TEMP].count);
    }
    // Today: the closed hours plus the one still open, each exactly once.
    g_chart.clear();
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::ChartRead::Read),
                      static_cast<int>(rig.history.hourlyChart(kNov10, kNov10 + 24 * kH, &collectChart, nullptr)));
    TEST_ASSERT_EQUAL_UINT32(6, g_chart.size());
    TEST_ASSERT_EQUAL_UINT32(kNov10 + 5 * kH, g_chart.back().start_epoch);
    uint32_t first = 0;
    TEST_ASSERT_TRUE(rig.history.firstDayKey(first));
    TEST_ASSERT_EQUAL_UINT32(20261107, first);
}

void test_chart_reads_refused_when_off() {
    Rig rig;
    rig.history.setEnabled(false);
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::ChartRead::Off),
                      static_cast<int>(rig.history.hourlyChart(kNov9, kNov10, &collectChart, nullptr)));
}

void test_operations_refused_when_off() {
    Rig rig;
    rig.history.setEnabled(false);
    LongHistory::OpResults r;
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::OpStatus::Off), static_cast<int>(rig.history.clearAll(r)));
    TEST_ASSERT_TRUE(r.items.empty());
}

void test_card_full_auto_delete_removes_the_oldest_month_only() {
    writeQuarters(kNov8 - 40U * 86400U, kNov8 - 38U * 86400U); // Sept 29 - 30
    writeQuarters(kNov8, kNov10 + 6 * kH);
    Rig rig;
    const time_t now = static_cast<time_t>(kNov10 + 6 * kH);
    rig.history.poll(now, true, LongHistory::Space{}); // prepares, rebuilds, measures the rate
    LongHistory::Space full;
    full.known = true;
    full.free_bytes = 10; // less than one day
    rig.history.poll(now, true, full);
    TEST_ASSERT_EQUAL(static_cast<int>(StorageHealth::Kind::Full), static_cast<int>(rig.history.health().kind));
    TEST_ASSERT_FALSE(g_root->exists("/aura/history/15min/2026/09"));
    TEST_ASSERT_TRUE(g_root->exists("/aura/history/15min/2026/11/2026-11-08.csv"));
    TEST_ASSERT_TRUE(g_root->exists(kHourly)); // summaries are never deleted
    // Still full at the next reading; only the current month is left and it
    // is never deleted, so recording stops.
    full.free_bytes = 11;
    rig.history.poll(now, true, full);
    TEST_ASSERT_EQUAL(static_cast<int>(StorageHealth::Kind::RecordingStopped),
                      static_cast<int>(rig.history.health().kind));
    TEST_ASSERT_TRUE(g_root->exists("/aura/history/15min/2026/11/2026-11-10.csv"));
    // Space again: recording resumes.
    LongHistory::Space room;
    room.known = true;
    room.free_bytes = 1ULL << 40;
    rig.history.poll(now, true, room);
    TEST_ASSERT_EQUAL(static_cast<int>(StorageHealth::Kind::Ok), static_cast<int>(rig.history.health().kind));
}

void test_card_full_stop_recording_deletes_nothing() {
    writeQuarters(kNov8 - 40U * 86400U, kNov8 - 38U * 86400U);
    writeQuarters(kNov8, kNov10 + 6 * kH);
    Rig rig;
    rig.history.setFullAction(StorageHealth::FullAction::StopRecording);
    const time_t now = static_cast<time_t>(kNov10 + 6 * kH);
    rig.history.poll(now, true, LongHistory::Space{});
    LongHistory::Space full;
    full.known = true;
    full.free_bytes = 10;
    rig.history.poll(now, true, full);
    TEST_ASSERT_EQUAL(static_cast<int>(StorageHealth::Kind::RecordingStopped),
                      static_cast<int>(rig.history.health().kind));
    TEST_ASSERT_TRUE(g_root->exists("/aura/history/15min/2026/09"));
}

void test_low_space_uses_the_measured_rate() {
    writeQuarters(kNov8, kNov10 + 6 * kH);
    Rig rig;
    const time_t now = static_cast<time_t>(kNov10 + 6 * kH);
    rig.history.poll(now, true, LongHistory::Space{});
    const std::string nov9 = g_root->read("/aura/history/15min/2026/11/2026-11-09.csv");
    LongHistory::Space space;
    space.known = true;
    space.free_bytes = static_cast<uint64_t>(nov9.size()) * 10U; // well under 30 days with summaries
    rig.history.poll(now, true, space);
    const StorageHealth::Status s = rig.history.health();
    TEST_ASSERT_EQUAL(static_cast<int>(StorageHealth::Kind::LowSpace), static_cast<int>(s.kind));
    TEST_ASSERT_TRUE(s.days_left >= 7 && s.days_left <= 10);
}

void test_clear_future_needs_a_trusted_clock_and_removes_only_the_future() {
    writeQuarters(kNov8, kNov10 + 6 * kH);
    writeQuarters(kNov10 + 400U * 86400U, kNov10 + 401U * 86400U); // clock once set ahead
    Rig rig;
    const time_t now = static_cast<time_t>(kNov10 + 6 * kH);
    LongHistory::OpResults r;
    rig.history.poll(now, false, LongHistory::Space{});
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::OpStatus::NoTime), static_cast<int>(rig.history.clearFuture(now, r)));
    TEST_ASSERT_TRUE(g_root->exists("/aura/history/15min/2027"));
    rig.history.poll(now, true, LongHistory::Space{});
    TEST_ASSERT_EQUAL(static_cast<int>(LongHistory::OpStatus::Done), static_cast<int>(rig.history.clearFuture(now, r)));
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_FALSE(g_root->exists("/aura/history/15min/2027/12"));
    TEST_ASSERT_EQUAL(24, static_cast<int>(quarters(rig.sd, kQ10).size()));
    TEST_ASSERT_EQUAL(96, static_cast<int>(quarters(rig.sd, kQ9).size()));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_daily_csv_uses_the_upstream_format);
    RUN_TEST(test_second_pass_with_the_limit_matches_the_first);
    RUN_TEST(test_clear_all_removes_every_group_and_the_old_daily_file);
    RUN_TEST(test_failed_removal_is_reported);
    RUN_TEST(test_clear_today_cuts_only_today_across_two_utc_files);
    RUN_TEST(test_operations_refused_when_off);
    RUN_TEST(test_hourly_chart_reads_the_range_and_the_hour_in_progress);
    RUN_TEST(test_chart_reads_refused_when_off);
    RUN_TEST(test_card_full_auto_delete_removes_the_oldest_month_only);
    RUN_TEST(test_card_full_stop_recording_deletes_nothing);
    RUN_TEST(test_low_space_uses_the_measured_rate);
    RUN_TEST(test_clear_future_needs_a_trusted_clock_and_removes_only_the_future);
    return UNITY_END();
}
