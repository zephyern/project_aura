// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string.h>
#include <vector>

#include "ArduinoMock.h"
#include "core/Logger.h"
#include "modules/HistoryBucket.h"

using namespace HistoryBucket;

namespace {

constexpr uint32_t kDay = 1794355200; // 2026-11-11T00:00:00Z
constexpr uint32_t kQ = QuarterHourRow::kQuarterSeconds;
constexpr uint32_t kHour = 3600;
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;
constexpr int CO2 = ChartsHistory::METRIC_CO2;
constexpr int GAS = ChartsHistory::METRIC_OPTIONAL_GAS;

// Raw samples, one per second-offset, so brute force can be compared.
struct Sample {
    uint32_t epoch;
    float value;
};

QuarterHourRow::Row quarterFrom(uint32_t start, const std::vector<Sample> &samples) {
    QuarterHourRow::Row row;
    row.start_epoch = start;
    for (const Sample &s : samples) {
        if (s.epoch < start || s.epoch >= start + kQ) continue;
        HistoryAggregate one;
        one.sum = s.value;
        one.count = 1;
        one.min = one.max = s.value;
        one.min_epoch = one.max_epoch = s.epoch;
        TEST_ASSERT_EQUAL(static_cast<int>(HistoryCombineResult::Combined),
                          static_cast<int>(HistoryAggregates::combine(row.metrics[TEMP], one)));
    }
    return row;
}

std::vector<Sample> samplesForDay() {
    std::vector<Sample> out;
    for (uint32_t t = kDay; t < kDay + 3 * kHour; t += 37) {
        out.push_back({t, 18.0f + static_cast<float>((t * 7919U) % 1000U) / 100.0f});
    }
    return out;
}

} // namespace

void setUp() { Logger::resetRecentForTest(); }
void tearDown() {}

void test_bucket_equals_brute_force_over_raw_samples() {
    const std::vector<Sample> samples = samplesForDay();
    std::vector<QuarterHourRow::Row> rows;
    for (uint32_t q = kDay; q < kDay + 3 * kHour; q += kQ) rows.push_back(quarterFrom(q, samples));

    Record hour;
    Stats stats;
    TEST_ASSERT_TRUE(deriveBucket(rows.data(), rows.size(), kDay + kHour, kDay + 2 * kHour, hour, stats));
    double sum = 0;
    uint32_t count = 0;
    float lo = 1e9f, hi = -1e9f;
    uint32_t lo_t = 0, hi_t = 0;
    for (const Sample &s : samples) {
        if (s.epoch < kDay + kHour || s.epoch >= kDay + 2 * kHour) continue;
        sum += s.value;
        ++count;
        if (s.value < lo) { lo = s.value; lo_t = s.epoch; }
        if (s.value > hi) { hi = s.value; hi_t = s.epoch; }
    }
    TEST_ASSERT_EQUAL_UINT32(4, stats.added);
    TEST_ASSERT_EQUAL_UINT32(8, stats.outside);
    TEST_ASSERT_EQUAL_UINT32(4, hour.source_records);
    TEST_ASSERT_EQUAL_UINT32(count, hour.metrics[TEMP].count);
    // Sums of four exact quarter sums vs one pass: equal within one rounding
    // of the final addition order; the means agree to the last float bit.
    TEST_ASSERT_TRUE(static_cast<float>(hour.metrics[TEMP].mean()) == static_cast<float>(sum / count));
    TEST_ASSERT_TRUE(hour.metrics[TEMP].min == lo);
    TEST_ASSERT_EQUAL_UINT32(lo_t, hour.metrics[TEMP].min_epoch);
    TEST_ASSERT_TRUE(hour.metrics[TEMP].max == hi);
    TEST_ASSERT_EQUAL_UINT32(hi_t, hour.metrics[TEMP].max_epoch);
    TEST_ASSERT_TRUE(hour.metrics[CO2].empty());
}

void test_live_equals_rebuilt_from_coarser_steps() {
    // Hour buckets built from quarters, then a 3-hour bucket from those hours,
    // equals the 3-hour bucket built straight from the quarters.
    const std::vector<Sample> samples = samplesForDay();
    std::vector<QuarterHourRow::Row> rows;
    for (uint32_t q = kDay; q < kDay + 3 * kHour; q += kQ) rows.push_back(quarterFrom(q, samples));
    Builder three;
    TEST_ASSERT_TRUE(three.begin(kDay, kDay + 3 * kHour));
    for (uint32_t h = 0; h < 3; ++h) {
        Record hour;
        Stats st;
        TEST_ASSERT_TRUE(deriveBucket(rows.data(), rows.size(), kDay + h * kHour, kDay + (h + 1) * kHour, hour, st));
        TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added), static_cast<int>(three.add(hour)));
    }
    Record direct;
    Stats st;
    TEST_ASSERT_TRUE(deriveBucket(rows.data(), rows.size(), kDay, kDay + 3 * kHour, direct, st));
    const HistoryAggregate &a = three.record().metrics[TEMP];
    const HistoryAggregate &b = direct.metrics[TEMP];
    TEST_ASSERT_EQUAL_UINT32(b.count, a.count);
    TEST_ASSERT_TRUE(a.min == b.min && a.max == b.max);
    TEST_ASSERT_EQUAL_UINT32(b.min_epoch, a.min_epoch);
    TEST_ASSERT_EQUAL_UINT32(b.max_epoch, a.max_epoch);
    TEST_ASSERT_TRUE(static_cast<float>(a.mean()) == static_cast<float>(b.mean()));
    TEST_ASSERT_EQUAL_UINT32(3, three.record().source_records);
}

void test_partial_bucket_keeps_real_count() {
    const std::vector<Sample> samples = samplesForDay();
    std::vector<QuarterHourRow::Row> rows = {quarterFrom(kDay + kQ, samples)}; // one quarter only
    Record hour;
    Stats st;
    TEST_ASSERT_TRUE(deriveBucket(rows.data(), rows.size(), kDay, kDay + kHour, hour, st));
    TEST_ASSERT_EQUAL_UINT32(rows[0].metrics[TEMP].count, hour.metrics[TEMP].count);
    TEST_ASSERT_EQUAL_UINT32(1, hour.source_records);
    Record none;
    TEST_ASSERT_TRUE(deriveBucket(rows.data(), rows.size(), kDay + kHour, kDay + 2 * kHour, none, st));
    TEST_ASSERT_TRUE(none.empty());
}

void test_out_of_order_and_invalid_are_refused_and_logged() {
    const std::vector<Sample> samples = samplesForDay();
    Builder b;
    TEST_ASSERT_FALSE(b.begin(kDay, kDay));
    TEST_ASSERT_TRUE(b.begin(kDay, kDay + kHour));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Added), static_cast<int>(b.add(quarterFrom(kDay + kQ, samples))));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::OutOfOrder), static_cast<int>(b.add(quarterFrom(kDay, samples))));
    TEST_ASSERT_EQUAL(static_cast<int>(AddResult::Outside),
                      static_cast<int>(b.add(quarterFrom(kDay + kHour, samples))));
    TEST_ASSERT_EQUAL_UINT32(1, b.stats().refused);
    TEST_ASSERT_EQUAL_UINT32(1, b.stats().outside);
    Logger::RecentEntry entries[16];
    const size_t n = Logger::copyRecent(entries, 16);
    bool logged = false;
    for (size_t i = 0; i < n; ++i) logged = logged || strstr(entries[i].message, "out of order");
    TEST_ASSERT_TRUE(logged);
}

void test_gas_type_change_keeps_newer_gas() {
    QuarterHourRow::Row a, b;
    a.start_epoch = kDay;
    b.start_epoch = kDay + kQ;
    a.metrics[GAS] = {10.0, 5, 1.0f, 3.0f, kDay, kDay + 1, 1};
    b.metrics[GAS] = {20.0, 4, 4.0f, 6.0f, kDay + kQ, kDay + kQ + 2, 2};
    const QuarterHourRow::Row rows[] = {a, b};
    Record out;
    Stats st;
    TEST_ASSERT_TRUE(deriveBucket(rows, 2, kDay, kDay + kHour, out, st));
    TEST_ASSERT_EQUAL_UINT8(2, out.metrics[GAS].source);
    TEST_ASSERT_EQUAL_UINT32(4, out.metrics[GAS].count);
    TEST_ASSERT_EQUAL_UINT32(1, st.gas_replaced);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_bucket_equals_brute_force_over_raw_samples);
    RUN_TEST(test_live_equals_rebuilt_from_coarser_steps);
    RUN_TEST(test_partial_bucket_keeps_real_count);
    RUN_TEST(test_out_of_order_and_invalid_are_refused_and_logged);
    RUN_TEST(test_gas_type_change_keeps_newer_gas);
    return UNITY_END();
}
