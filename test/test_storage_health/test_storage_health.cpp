// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include "modules/StorageHealth.h"

using namespace StorageHealth;

namespace {

Inputs healthy() {
    Inputs in;
    in.log_enabled = true;
    in.card_ready = true;
    in.free_bytes = 1000ULL * 100000ULL; // 1000 days at 100 kB/day
    in.bytes_per_day = 100000;
    return in;
}

int kind(const Inputs &in) { return static_cast<int>(evaluate(in).kind); }

} // namespace

void setUp() {}
void tearDown() {}

void test_off_and_ok() {
    Inputs in = healthy();
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Ok), kind(in));
    in.log_enabled = false;
    in.card_ready = false;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Off), kind(in));
}

void test_no_card() {
    Inputs in = healthy();
    in.card_ready = false;
    in.write_error = true;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::NoCard), kind(in));
}

void test_low_space_threshold_and_days() {
    Inputs in = healthy();
    in.free_bytes = 30ULL * 100000ULL;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Ok), kind(in)); // exactly 30 days
    in.free_bytes -= 1;
    const Status s = evaluate(in);
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::LowSpace), static_cast<int>(s.kind));
    TEST_ASSERT_EQUAL_UINT32(29, s.days_left);
}

void test_unmeasured_rate_is_never_low() {
    Inputs in = healthy();
    in.bytes_per_day = 0;
    in.free_bytes = 10;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Ok), kind(in));
}

void test_full_by_space_and_by_no_space_error() {
    Inputs in = healthy();
    in.free_bytes = 99999;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Full), kind(in));
    in = healthy();
    in.write_error = true;
    in.failure_kind = DailyStorageFailureKind::NoSpace;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Full), kind(in));
}

void test_recording_stopped() {
    Inputs in = healthy();
    in.free_bytes = 10;
    in.full_action = FullAction::StopRecording;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::RecordingStopped), kind(in));
    in.full_action = FullAction::AutoDelete;
    in.nothing_left_to_delete = true;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::RecordingStopped), kind(in));
    in.free_bytes = healthy().free_bytes; // space again: not stopped
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Ok), kind(in));
}

void test_write_error_carries_kind_and_beats_clock_behind() {
    Inputs in = healthy();
    in.write_error = true;
    in.failure_kind = DailyStorageFailureKind::Io;
    in.clock_behind = true;
    const Status s = evaluate(in);
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::WriteError), static_cast<int>(s.kind));
    TEST_ASSERT_EQUAL(static_cast<int>(DailyStorageFailureKind::Io), static_cast<int>(s.failure_kind));
    in.write_error = false;
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::ClockBehind), kind(in));
}

void test_bytes_per_day_is_measured() {
    const uint32_t days[] = {90000, 100000, 110000};
    RateSample s;
    TEST_ASSERT_EQUAL_UINT32(0, bytesPerDay(s));
    s.quarter_file_bytes = days;
    s.days = 3;
    TEST_ASSERT_EQUAL_UINT32(100000, bytesPerDay(s));
    s.hourly_record_bytes = 1000;
    s.daily_record_bytes = 1100;
    TEST_ASSERT_EQUAL_UINT32(100000 + 24000 + 1100, bytesPerDay(s));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, daysLeft(5, 0));
    TEST_ASSERT_EQUAL_UINT32(2, daysLeft(250, 100));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_off_and_ok);
    RUN_TEST(test_no_card);
    RUN_TEST(test_low_space_threshold_and_days);
    RUN_TEST(test_unmeasured_rate_is_never_low);
    RUN_TEST(test_full_by_space_and_by_no_space_error);
    RUN_TEST(test_recording_stopped);
    RUN_TEST(test_write_error_carries_kind_and_beats_clock_behind);
    RUN_TEST(test_bytes_per_day_is_measured);
    return UNITY_END();
}
