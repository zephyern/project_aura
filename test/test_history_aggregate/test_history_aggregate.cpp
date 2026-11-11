// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <stdint.h>
#include <vector>

#include "modules/HistoryAggregate.h"

namespace {

struct Sample {
    float value;
    uint32_t epoch;
};

HistoryAggregate fromSamples(const std::vector<Sample> &samples, uint8_t source = 0) {
    HistoryAggregate a;
    a.source = source;
    for (const Sample &s : samples) {
        if (a.count == 0 || s.value < a.min) {
            a.min = s.value;
            a.min_epoch = s.epoch;
        }
        if (a.count == 0 || s.value > a.max) {
            a.max = s.value;
            a.max_epoch = s.epoch;
        }
        a.sum += s.value;
        ++a.count;
    }
    return a;
}

} // namespace

void setUp() {}
void tearDown() {}

void test_weighted_mean_matches_brute_force_over_all_samples() {
    std::vector<Sample> first, second, all;
    for (uint32_t n = 0; n < 300; ++n) {
        first.push_back({20.0f + 0.013f * static_cast<float>(n), 1000 + n});
    }
    for (uint32_t n = 0; n < 7; ++n) {
        second.push_back({35.5f - 0.7f * static_cast<float>(n), 2000 + n});
    }
    all = first;
    all.insert(all.end(), second.begin(), second.end());

    HistoryAggregate combined = fromSamples(first);
    TEST_ASSERT_EQUAL(HistoryCombineResult::Combined,
                      HistoryAggregates::combine(combined, fromSamples(second)));
    const HistoryAggregate expected = fromSamples(all);

    TEST_ASSERT_EQUAL_UINT32(307, combined.count);
    TEST_ASSERT_TRUE(expected.sum == combined.sum);
    TEST_ASSERT_TRUE(expected.mean() == combined.mean());
    // An average of the two means would be wrong by a wide margin here.
    const double naive = (fromSamples(first).mean() + fromSamples(second).mean()) / 2.0;
    TEST_ASSERT_TRUE(naive - combined.mean() > 5.0);
}

void test_min_max_and_their_times_follow_the_extreme_side() {
    HistoryAggregate a = fromSamples({{10.0f, 100}, {12.0f, 101}});
    const HistoryAggregate b = fromSamples({{8.0f, 200}, {15.0f, 205}});
    TEST_ASSERT_EQUAL(HistoryCombineResult::Combined, HistoryAggregates::combine(a, b));
    TEST_ASSERT_EQUAL_FLOAT(8.0f, a.min);
    TEST_ASSERT_EQUAL_UINT32(200, a.min_epoch);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, a.max);
    TEST_ASSERT_EQUAL_UINT32(205, a.max_epoch);
}

void test_equal_extremes_keep_the_earlier_time_in_either_order() {
    const HistoryAggregate early = fromSamples({{5.0f, 100}, {9.0f, 110}});
    const HistoryAggregate late = fromSamples({{5.0f, 300}, {9.0f, 310}});

    HistoryAggregate forward = early;
    TEST_ASSERT_EQUAL(HistoryCombineResult::Combined, HistoryAggregates::combine(forward, late));
    HistoryAggregate backward = late;
    TEST_ASSERT_EQUAL(HistoryCombineResult::Combined, HistoryAggregates::combine(backward, early));

    for (const HistoryAggregate *a : {&forward, &backward}) {
        TEST_ASSERT_EQUAL_UINT32(100, a->min_epoch);
        TEST_ASSERT_EQUAL_UINT32(110, a->max_epoch);
        TEST_ASSERT_EQUAL_UINT32(4, a->count);
    }
}

void test_empty_sides_contribute_nothing() {
    const HistoryAggregate data = fromSamples({{3.0f, 50}}, 7);
    HistoryAggregate into;
    TEST_ASSERT_EQUAL(HistoryCombineResult::Combined, HistoryAggregates::combine(into, data));
    TEST_ASSERT_EQUAL_UINT32(1, into.count);
    TEST_ASSERT_EQUAL_UINT8(7, into.source);
    TEST_ASSERT_EQUAL_UINT32(50, into.min_epoch);

    // An empty aggregate never conflicts, whatever its source.
    HistoryAggregate empty_other_source;
    empty_other_source.source = 9;
    TEST_ASSERT_EQUAL(HistoryCombineResult::Combined,
                      HistoryAggregates::combine(into, empty_other_source));
    TEST_ASSERT_EQUAL_UINT32(1, into.count);
    TEST_ASSERT_EQUAL_UINT8(7, into.source);

    HistoryAggregate both_empty;
    TEST_ASSERT_EQUAL(HistoryCombineResult::Combined,
                      HistoryAggregates::combine(both_empty, HistoryAggregate{}));
    TEST_ASSERT_TRUE(both_empty.empty());
}

void test_different_sources_are_refused_and_left_unchanged() {
    HistoryAggregate a = fromSamples({{1.0f, 10}, {2.0f, 11}}, 3);
    const HistoryAggregate before = a;
    TEST_ASSERT_EQUAL(HistoryCombineResult::SourceMismatch,
                      HistoryAggregates::combine(a, fromSamples({{0.5f, 20}}, 4)));
    TEST_ASSERT_EQUAL_UINT32(before.count, a.count);
    TEST_ASSERT_TRUE(before.sum == a.sum);
    TEST_ASSERT_EQUAL_FLOAT(before.min, a.min);
    TEST_ASSERT_EQUAL_UINT8(3, a.source);
}

void test_count_overflow_is_refused_and_left_unchanged() {
    HistoryAggregate a = fromSamples({{1.0f, 10}});
    a.count = UINT32_MAX;
    HistoryAggregate b = fromSamples({{2.0f, 20}});
    TEST_ASSERT_EQUAL(HistoryCombineResult::CountOverflow, HistoryAggregates::combine(a, b));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, a.count);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, a.max);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_weighted_mean_matches_brute_force_over_all_samples);
    RUN_TEST(test_min_max_and_their_times_follow_the_extreme_side);
    RUN_TEST(test_equal_extremes_keep_the_earlier_time_in_either_order);
    RUN_TEST(test_empty_sides_contribute_nothing);
    RUN_TEST(test_different_sources_are_refused_and_left_unchanged);
    RUN_TEST(test_count_overflow_is_refused_and_left_unchanged);
    return UNITY_END();
}
