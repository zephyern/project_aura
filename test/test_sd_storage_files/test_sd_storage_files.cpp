// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

// SdCardManager file operations against a real host directory standing in for
// the card (setTestMountRoot).

#include <unity.h>

#include <string>
#include <map>

#include "ArduinoMock.h"
#include "core/WakePowerGuard.h"
#include "modules/SdCardManager.h"
#include "SdTempRoot.h"

namespace {

SdTempRoot *g_root = nullptr;

void writeHostFile(const std::string &relative, const std::string &content) {
    g_root->write(relative, content);
}

bool hostExists(const std::string &relative) { return g_root->exists(relative); }

struct Mounted {
    SdCardManager sd;
    Mounted() { g_root->mount(sd); }
};

struct Listing {
    std::map<std::string, bool> entries;
    size_t stop_after = 0;
    static bool add(const char *name, bool is_dir, void *ctx) {
        auto &l = *static_cast<Listing *>(ctx);
        l.entries[name] = is_dir;
        return l.stop_after == 0 || l.entries.size() < l.stop_after;
    }
};

} // namespace

void setUp() {
    setMillis(0);
    WakePowerGuard::resetForTest();
    g_root = new SdTempRoot();
}

void tearDown() {
    delete g_root;
    g_root = nullptr;
    WakePowerGuard::resetForTest();
}

void test_list_dir_reports_files_and_subdirectories() {
    Mounted m;
    writeHostFile("/aura/history/15min/2026/11/2026-11-10.csv", "x\n");
    writeHostFile("/aura/history/15min/2026/11/2026-11-11.csv", "y\n");
    writeHostFile("/aura/history/15min/2027/01/2027-01-01.csv", "z\n");
    Listing years;
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Listed),
                      static_cast<int>(m.sd.listDir("/aura/history/15min", &Listing::add, &years)));
    TEST_ASSERT_EQUAL(2, years.entries.size());
    TEST_ASSERT_TRUE(years.entries.at("2026"));
    TEST_ASSERT_TRUE(years.entries.at("2027"));
    Listing days;
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Listed),
                      static_cast<int>(m.sd.listDir("/aura/history/15min/2026/11", &Listing::add, &days)));
    TEST_ASSERT_EQUAL(2, days.entries.size());
    TEST_ASSERT_FALSE(days.entries.at("2026-11-10.csv"));
    TEST_ASSERT_FALSE(days.entries.at("2026-11-11.csv"));
}

void test_list_dir_stops_when_asked_and_reports_absent_without_a_fault() {
    Mounted m;
    writeHostFile("/d/a", "1");
    writeHostFile("/d/b", "2");
    writeHostFile("/d/c", "3");
    Listing first;
    first.stop_after = 1;
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Listed),
                      static_cast<int>(m.sd.listDir("/d", &Listing::add, &first)));
    TEST_ASSERT_EQUAL(1, first.entries.size());
    Listing none;
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Absent),
                      static_cast<int>(m.sd.listDir("/missing", &Listing::add, &none)));
    TEST_ASSERT_TRUE(none.entries.empty());
    TEST_ASSERT_TRUE(m.sd.status().runtime_healthy);
}

void test_list_dir_on_a_file_or_bad_arguments_fails_typed() {
    Mounted m;
    writeHostFile("/f", "1");
    Listing l;
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Failed),
                      static_cast<int>(m.sd.listDir("/f", &Listing::add, &l)));
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Failed),
                      static_cast<int>(m.sd.listDir(nullptr, &Listing::add, &l)));
    TEST_ASSERT_EQUAL(static_cast<int>(DailyStorageFailureKind::Invalid),
                      static_cast<int>(m.sd.lastFailureKind()));
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Failed),
                      static_cast<int>(m.sd.listDir("/", nullptr, nullptr)));
}

void test_remove_empty_dir_removes_only_empty_directories() {
    Mounted m;
    writeHostFile("/aura/history/15min/2026/11/2026-11-10.csv", "x\n");
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::NotEmpty),
                      static_cast<int>(m.sd.removeEmptyDir("/aura/history/15min/2026/11")));
    TEST_ASSERT_TRUE(hostExists("/aura/history/15min/2026/11/2026-11-10.csv"));
    TEST_ASSERT_TRUE(m.sd.status().runtime_healthy);
    TEST_ASSERT_TRUE(m.sd.removeFile("/aura/history/15min/2026/11/2026-11-10.csv"));
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::Removed),
                      static_cast<int>(m.sd.removeEmptyDir("/aura/history/15min/2026/11")));
    TEST_ASSERT_FALSE(hostExists("/aura/history/15min/2026/11"));
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::Absent),
                      static_cast<int>(m.sd.removeEmptyDir("/aura/history/15min/2026/11")));
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::Removed),
                      static_cast<int>(m.sd.removeEmptyDir("/aura/history/15min/2026")));
}

void test_remove_empty_dir_refuses_the_card_root_and_bad_arguments() {
    Mounted m;
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::Failed),
                      static_cast<int>(m.sd.removeEmptyDir("/")));
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::Failed),
                      static_cast<int>(m.sd.removeEmptyDir("")));
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::Failed),
                      static_cast<int>(m.sd.removeEmptyDir(nullptr)));
    TEST_ASSERT_EQUAL(static_cast<int>(DailyStorageFailureKind::Invalid),
                      static_cast<int>(m.sd.lastFailureKind()));
    TEST_ASSERT_TRUE(hostExists(""));
}

void test_unmounted_card_refuses_listing_and_removal() {
    SdCardManager sd;
    sd.setTestMountRoot(g_root->root().c_str());
    Listing l;
    TEST_ASSERT_EQUAL(static_cast<int>(DirListResult::Failed),
                      static_cast<int>(sd.listDir("/", &Listing::add, &l)));
    TEST_ASSERT_EQUAL(static_cast<int>(DailyStorageFailureKind::Busy),
                      static_cast<int>(sd.lastFailureKind()));
    TEST_ASSERT_EQUAL(static_cast<int>(DirRemoveResult::Failed),
                      static_cast<int>(sd.removeEmptyDir("/x")));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_list_dir_reports_files_and_subdirectories);
    RUN_TEST(test_list_dir_stops_when_asked_and_reports_absent_without_a_fault);
    RUN_TEST(test_list_dir_on_a_file_or_bad_arguments_fails_typed);
    RUN_TEST(test_remove_empty_dir_removes_only_empty_directories);
    RUN_TEST(test_remove_empty_dir_refuses_the_card_root_and_bad_arguments);
    RUN_TEST(test_unmounted_card_refuses_listing_and_removal);
    return UNITY_END();
}
