// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string.h>
#include <string>
#include <vector>

#include "ArduinoMock.h"
#include "SdTempRoot.h"
#include "core/Logger.h"
#include "core/WakePowerGuard.h"
#include "modules/LongHistoryFiles.h"
#include "modules/SdCardManager.h"

using namespace LongHistoryFiles;

namespace {

constexpr uint32_t kStart = 1767225600; // 2026-01-01T00:00:00Z
constexpr const char *kPath = "/aura/history/15min/2026/01/2026-01-01.csv";
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;

SdTempRoot *g_root = nullptr;

std::string header() {
    String h;
    QuarterHourRow::appendHeader(h);
    return h;
}

std::string row(uint32_t quarter, float value) {
    QuarterHourRow::Row r;
    r.start_epoch = kStart + quarter * QuarterHourRow::kQuarterSeconds;
    r.metrics[TEMP].sum = value;
    r.metrics[TEMP].count = 1;
    r.metrics[TEMP].min = r.metrics[TEMP].max = value;
    r.metrics[TEMP].min_epoch = r.metrics[TEMP].max_epoch = r.start_epoch + 5;
    String out;
    TEST_ASSERT_EQUAL(static_cast<int>(QuarterHourRow::EncodeResult::Ok),
                      static_cast<int>(QuarterHourRow::appendRow(r, out)));
    return out;
}

int warningsContaining(const char *text) {
    Logger::RecentEntry entries[64];
    const size_t n = Logger::copyRecent(entries, 64);
    int hits = 0;
    for (size_t i = 0; i < n; ++i) {
        if (strstr(entries[i].message, text)) ++hits;
    }
    return hits;
}

struct Collected {
    std::vector<uint32_t> starts;
    size_t stop_after = 0;
    static bool add(const QuarterHourRow::Row &r, size_t, void *ctx) {
        auto &c = *static_cast<Collected *>(ctx);
        c.starts.push_back(r.start_epoch);
        return c.stop_after == 0 || c.starts.size() < c.stop_after;
    }
};

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

void test_quarter_file_path_is_named_by_utc_date() {
    char path[kPathCapacity];
    TEST_ASSERT_TRUE(quarterFilePath(20261110, path));
    TEST_ASSERT_EQUAL_STRING("/aura/history/15min/2026/11/2026-11-10.csv", path);
    TEST_ASSERT_TRUE(quarterFilePath(20280229, path));
    TEST_ASSERT_FALSE(quarterFilePath(20270229, path));
    TEST_ASSERT_FALSE(quarterFilePath(20191231, path));
    TEST_ASSERT_FALSE(quarterFilePath(20261301, path));
}

void test_newest_file_skips_foreign_names_and_empty_directories() {
    SdCardManager sd;
    g_root->mount(sd);
    uint32_t key = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(FindResult::None), static_cast<int>(findNewestQuarterFile(sd, key)));
    g_root->write("/aura/history/15min/2026/10/2026-10-31.csv", "x");
    g_root->write("/aura/history/15min/2026/11/2026-11-09.csv", "x");
    g_root->write("/aura/history/15min/2026/11/2026-11-10.csv", "x");
    g_root->write("/aura/history/15min/2026/11/2026-11-30.csv.tmp", "x");
    g_root->write("/aura/history/15min/2026/11/2026-12-01.csv", "x"); // wrong month dir
    g_root->write("/aura/history/15min/2026/11/notes.txt", "x");
    g_root->write("/aura/history/15min/2027/01/readme", "x"); // newer year, no day files
    g_root->write("/aura/history/15min/2099x/01/2099-01-01.csv", "x");
    TEST_ASSERT_EQUAL(static_cast<int>(FindResult::Found), static_cast<int>(findNewestQuarterFile(sd, key)));
    TEST_ASSERT_EQUAL_UINT32(20261110, key);
}

void test_clean_file_is_not_rewritten() {
    SdCardManager sd;
    g_root->mount(sd);
    const std::string content = header() + row(0, 20) + row(1, 21);
    g_root->write(kPath, content);
    size_t removed = 99;
    TEST_ASSERT_EQUAL(static_cast<int>(RepairResult::Clean),
                      static_cast<int>(repairTail(sd, kPath, header().c_str(), removed)));
    TEST_ASSERT_EQUAL(0, removed);
    TEST_ASSERT_EQUAL_STRING(content.c_str(), g_root->read(kPath).c_str());
    TEST_ASSERT_FALSE(g_root->exists(std::string(kPath) + ".tmp"));
    TEST_ASSERT_EQUAL(0, warningsContaining("repaired"));
    TEST_ASSERT_EQUAL(static_cast<int>(RepairResult::Absent),
                      static_cast<int>(repairTail(sd, "/aura/none.csv", header().c_str(), removed)));
}

void test_torn_row_is_removed_with_one_log_line() {
    SdCardManager sd;
    g_root->mount(sd);
    const std::string good = header() + row(0, 20) + row(1, 21);
    // A cut append: part of a row, no '\n'. Longer than one 256-byte scan step.
    // (A full row with every metric is about 1 KB.)
    std::string torn;
    while (torn.size() < 300) torn += row(2, 22).substr(0, row(2, 22).size() - 1) + ",";
    torn.resize(300);
    TEST_ASSERT_EQUAL(std::string::npos, torn.find('\n'));
    g_root->write(kPath, good + torn);
    size_t removed = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(RepairResult::Repaired),
                      static_cast<int>(repairTail(sd, kPath, header().c_str(), removed)));
    const size_t expected = 300;
    TEST_ASSERT_EQUAL(expected, removed);
    TEST_ASSERT_EQUAL_STRING(good.c_str(), g_root->read(kPath).c_str());
    TEST_ASSERT_EQUAL(1, warningsContaining(("removed " + std::to_string(expected) + " bytes").c_str()));
    TEST_ASSERT_FALSE(g_root->exists(std::string(kPath) + ".tmp"));
    TEST_ASSERT_FALSE(g_root->exists(std::string(kPath) + ".bak"));
}

void test_torn_header_is_rewritten() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write(kPath, header().substr(0, 700));
    size_t removed = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(RepairResult::HeaderRewritten),
                      static_cast<int>(repairTail(sd, kPath, header().c_str(), removed)));
    TEST_ASSERT_EQUAL(700, removed);
    TEST_ASSERT_EQUAL_STRING(header().c_str(), g_root->read(kPath).c_str());
    TEST_ASSERT_EQUAL(1, warningsContaining("torn header"));
}

void test_reader_delivers_rows_and_skips_bad_middle_line_with_a_log() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write(kPath, header() + row(0, 20) + "1767226500,garbage\n" + row(2, 22) + row(3, 23));
    Collected c;
    ReadStats stats;
    TEST_ASSERT_EQUAL(static_cast<int>(ReadResult::Read),
                      static_cast<int>(readQuarterFile(sd, kPath, &Collected::add, &c, stats)));
    TEST_ASSERT_EQUAL(3, c.starts.size());
    TEST_ASSERT_EQUAL_UINT32(kStart, c.starts[0]);
    TEST_ASSERT_EQUAL_UINT32(kStart + 2 * 900, c.starts[1]);
    TEST_ASSERT_EQUAL_UINT32(3, stats.rows);
    TEST_ASSERT_EQUAL_UINT32(1, stats.skipped);
    TEST_ASSERT_EQUAL(1, warningsContaining("skipped line 3 (malformed)"));
}

void test_reader_treats_unterminated_last_line_as_torn_and_can_stop_early() {
    SdCardManager sd;
    g_root->mount(sd);
    const std::string last = row(1, 21);
    g_root->write(kPath, header() + row(0, 20) + last.substr(0, last.size() - 1));
    Collected c;
    ReadStats stats;
    TEST_ASSERT_EQUAL(static_cast<int>(ReadResult::Read),
                      static_cast<int>(readQuarterFile(sd, kPath, &Collected::add, &c, stats)));
    TEST_ASSERT_EQUAL(1, c.starts.size());
    TEST_ASSERT_EQUAL_UINT32(1, stats.skipped);
    TEST_ASSERT_EQUAL(1, warningsContaining("unterminated"));
    g_root->write(kPath, header() + row(0, 20) + row(1, 21) + row(2, 22));
    Collected first;
    first.stop_after = 1;
    TEST_ASSERT_EQUAL(static_cast<int>(ReadResult::Read),
                      static_cast<int>(readQuarterFile(sd, kPath, &Collected::add, &first, stats)));
    TEST_ASSERT_EQUAL(1, first.starts.size());
}

void test_reader_rejects_a_foreign_header_and_reports_absent_and_empty() {
    SdCardManager sd;
    g_root->mount(sd);
    Collected c;
    ReadStats stats;
    g_root->write(kPath, "date,metric,unit\n" + row(0, 20));
    TEST_ASSERT_EQUAL(static_cast<int>(ReadResult::BadHeader),
                      static_cast<int>(readQuarterFile(sd, kPath, &Collected::add, &c, stats)));
    TEST_ASSERT_TRUE(c.starts.empty());
    g_root->write(kPath, "");
    TEST_ASSERT_EQUAL(static_cast<int>(ReadResult::Empty),
                      static_cast<int>(readQuarterFile(sd, kPath, &Collected::add, &c, stats)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReadResult::Absent),
                      static_cast<int>(readQuarterFile(sd, "/aura/x.csv", &Collected::add, &c, stats)));
}

void test_range_read_and_truncate_atomic_on_the_card() {
    SdCardManager sd;
    g_root->mount(sd);
    g_root->write("/f.txt", "0123456789");
    char buf[8];
    size_t got = 0;
    TEST_ASSERT_TRUE(sd.readRange("/f.txt", 7, buf, sizeof(buf), got));
    TEST_ASSERT_EQUAL(3, got);
    TEST_ASSERT_EQUAL_MEMORY("789", buf, 3);
    TEST_ASSERT_TRUE(sd.readRange("/f.txt", 20, buf, sizeof(buf), got));
    TEST_ASSERT_EQUAL(0, got);
    TEST_ASSERT_FALSE(sd.readRange("/missing", 0, buf, 1, got));
    TEST_ASSERT_EQUAL(static_cast<int>(DailyStorageFailureKind::Missing),
                      static_cast<int>(sd.lastFailureKind()));
    TEST_ASSERT_TRUE(sd.truncateAtomic("/f.txt", 4));
    TEST_ASSERT_EQUAL_STRING("0123", g_root->read("/f.txt").c_str());
    TEST_ASSERT_FALSE(sd.truncateAtomic("/f.txt", 5)); // longer than the file
    TEST_ASSERT_EQUAL_STRING("0123", g_root->read("/f.txt").c_str());
    TEST_ASSERT_FALSE(g_root->exists("/f.txt.tmp"));
    TEST_ASSERT_EQUAL(static_cast<int>(DailyStorageFailureKind::Invalid),
                      static_cast<int>(sd.lastFailureKind()));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_quarter_file_path_is_named_by_utc_date);
    RUN_TEST(test_newest_file_skips_foreign_names_and_empty_directories);
    RUN_TEST(test_clean_file_is_not_rewritten);
    RUN_TEST(test_torn_row_is_removed_with_one_log_line);
    RUN_TEST(test_torn_header_is_rewritten);
    RUN_TEST(test_reader_delivers_rows_and_skips_bad_middle_line_with_a_log);
    RUN_TEST(test_reader_treats_unterminated_last_line_as_torn_and_can_stop_early);
    RUN_TEST(test_reader_rejects_a_foreign_header_and_reports_absent_and_empty);
    RUN_TEST(test_range_read_and_truncate_atomic_on_the_card);
    return UNITY_END();
}
