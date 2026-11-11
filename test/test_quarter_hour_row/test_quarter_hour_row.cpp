// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string.h>
#include <string>

#include "modules/QuarterHourRow.h"

using namespace QuarterHourRow;

namespace {

constexpr uint32_t kStart = 1767225600; // 2026-01-01T00:00:00Z, a quarter boundary.
constexpr int TEMP = ChartsHistory::METRIC_TEMPERATURE;
constexpr int CO2 = ChartsHistory::METRIC_CO2;
constexpr int GAS = ChartsHistory::METRIC_OPTIONAL_GAS;
constexpr int OUT = ChartsHistory::METRIC_OUTDOOR_TEMP;
constexpr uint8_t API = ChartsHistory::OUTDOOR_SOURCE_API;

HistoryAggregate agg(double sum, uint32_t count, float lo, uint32_t lo_s, float hi,
                     uint32_t hi_s, uint8_t source = 0) {
    HistoryAggregate a;
    a.sum = sum;
    a.count = count;
    a.min = lo;
    a.max = hi;
    a.min_epoch = kStart + lo_s;
    a.max_epoch = kStart + hi_s;
    a.source = source;
    return a;
}

Row sampleRow() {
    Row row;
    row.start_epoch = kStart;
    row.metrics[TEMP] = agg(0.1 + 0.2 + 21.123456789012345, 3, -3.25f, 17, 21.5f, 899);
    row.metrics[CO2] = agg(1234567.0000000002, 1500, 412.0f, 0, 1999.5f, 450);
    row.metrics[GAS] = agg(3.5, 7, 0.25f, 300, 1.125f, 600, 2);
    return row;
}

std::string header() {
    String out;
    appendHeader(out);
    return out;
}

std::string encode(const Row &row) {
    String out;
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::Ok), static_cast<int>(appendRow(row, out)));
    return out;
}

DecodeResult decodeLine(const std::string &with_newline, Row &out) {
    std::string line = with_newline;
    if (!line.empty() && line.back() == '\n') line.pop_back();
    return decodeRow(line.c_str(), line.size(), out);
}

DecodeResult decodeHeaderLine(const std::string &with_newline) {
    std::string line = with_newline;
    if (!line.empty() && line.back() == '\n') line.pop_back();
    return decodeHeader(line.c_str(), line.size());
}

void assertSame(const HistoryAggregate &a, const HistoryAggregate &b) {
    TEST_ASSERT_EQUAL_UINT32(a.count, b.count);
    if (a.count == 0) return;
    TEST_ASSERT_TRUE(a.sum == b.sum); // exact, bit for bit
    TEST_ASSERT_TRUE(a.min == b.min);
    TEST_ASSERT_TRUE(a.max == b.max);
    TEST_ASSERT_EQUAL_UINT32(a.min_epoch, b.min_epoch);
    TEST_ASSERT_EQUAL_UINT32(a.max_epoch, b.max_epoch);
    TEST_ASSERT_EQUAL_UINT8(a.source, b.source);
}

} // namespace

void setUp() {}
void tearDown() {}

void test_header_lists_every_metric_and_round_trips() {
    const std::string h = header();
    TEST_ASSERT_EQUAL('\n', h.back());
    TEST_ASSERT_EQUAL(0, h.find("start_utc_v1,co2_sum,co2_count,co2_min,co2_min_s,co2_max,co2_max_s,"
                                "temperature_sum"));
    TEST_ASSERT_TRUE(h.find(",optional_gas_max_s,outdoor_temp_sum,") != std::string::npos);
    TEST_ASSERT_TRUE(h.find(",outdoor_temp_max_s,optional_gas_type,outdoor_temp_source\n") != std::string::npos);
    size_t commas = 0;
    for (char c : h) commas += c == ',';
    TEST_ASSERT_EQUAL(kFieldCount - 1, commas);
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Ok), static_cast<int>(decodeHeaderLine(h)));
}

void test_row_round_trips_exactly_including_empty_metrics() {
    const Row row = sampleRow();
    const std::string line = encode(row);
    Row back;
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Ok), static_cast<int>(decodeLine(line, back)));
    TEST_ASSERT_EQUAL_UINT32(kStart, back.start_epoch);
    for (int m = 0; m < ChartsHistory::kMetricCount; ++m) assertSame(row.metrics[m], back.metrics[m]);
    TEST_ASSERT_EQUAL_UINT32(0, back.metrics[ChartsHistory::METRIC_VOC].count);
    // Empty metric: count 0, the other five fields empty.
    TEST_ASSERT_TRUE(line.find(",,0,,,,") != std::string::npos);
    TEST_ASSERT_TRUE(line.find(",2,\n") == line.size() - 4);  // gas type, no outdoor source
}

void test_numbers_use_a_dot_and_never_quotes() {
    const std::string line = encode(sampleRow());
    TEST_ASSERT_TRUE(line.find("-3.25") != std::string::npos);
    TEST_ASSERT_TRUE(line.find('"') == std::string::npos);
    TEST_ASSERT_TRUE(line.find(' ') == std::string::npos);
}

void test_encode_refuses_bad_rows_and_leaves_output_unchanged() {
    String out = "keep";
    Row row = sampleRow();
    row.start_epoch = kStart + 60;
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::BadStart), static_cast<int>(appendRow(row, out)));
    row = sampleRow();
    row.metrics[TEMP].sum = 1.0 / 0.0;
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::NonFinite), static_cast<int>(appendRow(row, out)));
    row = sampleRow();
    row.metrics[TEMP].max_epoch = kStart + kQuarterSeconds;
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::TimeOutside), static_cast<int>(appendRow(row, out)));
    row = sampleRow();
    row.metrics[TEMP].source = 1;
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::BadSource), static_cast<int>(appendRow(row, out)));
    row = sampleRow();
    row.metrics[GAS].source = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::BadSource), static_cast<int>(appendRow(row, out)));
    row = sampleRow();
    row.metrics[OUT] = agg(10.0, 2, 4.0f, 0, 6.0f, 300, 0);  // outdoor data without its source
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::BadSource), static_cast<int>(appendRow(row, out)));
    row.metrics[OUT].source = 2;  // the reserved thermometer value is not written yet
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::BadSource), static_cast<int>(appendRow(row, out)));
    row = sampleRow();
    row.metrics[OUT].source = API;  // a source with no data
    TEST_ASSERT_EQUAL(static_cast<int>(EncodeResult::BadSource), static_cast<int>(appendRow(row, out)));
    TEST_ASSERT_EQUAL_STRING("keep", out.c_str());
}

void test_outdoor_source_word_round_trips_and_mismatch_is_refused() {
    Row row = sampleRow();
    row.metrics[OUT] = agg(10.0, 2, 4.0f, 0, 6.0f, 300, API);
    const std::string line = encode(row);
    TEST_ASSERT_TRUE(line.find(",2,api\n") == line.size() - 7);
    Row back;
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Ok), static_cast<int>(decodeLine(line, back)));
    assertSame(row.metrics[OUT], back.metrics[OUT]);
    std::string other = line;
    other.replace(other.size() - 4, 3, "xyz");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadValue), static_cast<int>(decodeLine(other, back)));
    std::string missing = line;
    missing.replace(missing.size() - 4, 3, "");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadValue), static_cast<int>(decodeLine(missing, back)));
    const std::string empty = encode(sampleRow());
    std::string invented = empty;
    invented.insert(invented.size() - 1, "api");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadValue), static_cast<int>(decodeLine(invented, back)));
}

void test_header_errors_are_typed() {
    std::string h = header();
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::UnsupportedVersion),
                      static_cast<int>(decodeHeaderLine("start_utc_v2,co2_sum")));
    std::string unknown = h;
    unknown.replace(unknown.find("co2_sum"), 7, "co3_sum");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::UnknownColumn),
                      static_cast<int>(decodeHeaderLine(unknown)));
    std::string swapped = h;
    swapped.replace(swapped.find("co2_min,co2_min_s"), 17, "co2_min_s,co2_min");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadHeader),
                      static_cast<int>(decodeHeaderLine(swapped)));
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadHeader),
                      static_cast<int>(decodeHeaderLine(h.substr(0, h.size() / 2))));
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadHeader),
                      static_cast<int>(decodeHeaderLine("date,metric,unit")));
}

void test_row_errors_are_typed() {
    const std::string good = encode(sampleRow());
    Row out;
    // A torn row (power cut mid-write) has too few fields.
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Malformed),
                      static_cast<int>(decodeLine(good.substr(0, good.size() / 2), out)));
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Malformed),
                      static_cast<int>(decodeLine(good.substr(0, good.size() - 1) + ",1", out)));
    std::string quoted = good;
    quoted.replace(quoted.find("-3.25"), 5, "\"-3.25\"");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Malformed), static_cast<int>(decodeLine(quoted, out)));
    std::string comma_decimal = good;
    comma_decimal.replace(comma_decimal.find("-3.25"), 5, "-3 25");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Malformed),
                      static_cast<int>(decodeLine(comma_decimal, out)));
    std::string infinite = good;
    infinite.replace(infinite.find("-3.25"), 5, "-1e999");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::NonFinite), static_cast<int>(decodeLine(infinite, out)));
    std::string nan_text = good;
    nan_text.replace(nan_text.find("-3.25"), 5, "nan");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Malformed), static_cast<int>(decodeLine(nan_text, out)));
    std::string second = good;
    second.replace(second.find(",899,"), 5, ",900,");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadValue), static_cast<int>(decodeLine(second, out)));
    std::string start = good;
    start.replace(0, 10, std::to_string(kStart + 1));
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadValue), static_cast<int>(decodeLine(start, out)));
    std::string half_empty = good;
    half_empty.replace(half_empty.find(",,0,,,,"), 7, ",,0,1,,,");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Malformed),
                      static_cast<int>(decodeLine(half_empty, out)));
    Row no_type = sampleRow();
    std::string gas_untyped = encode(no_type);
    gas_untyped.replace(gas_untyped.size() - 4, 4, ",0,\n");
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::BadValue), static_cast<int>(decodeLine(gas_untyped, out)));
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeResult::Ok), static_cast<int>(decodeLine(good, out)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_header_lists_every_metric_and_round_trips);
    RUN_TEST(test_row_round_trips_exactly_including_empty_metrics);
    RUN_TEST(test_numbers_use_a_dot_and_never_quotes);
    RUN_TEST(test_encode_refuses_bad_rows_and_leaves_output_unchanged);
    RUN_TEST(test_outdoor_source_word_round_trips_and_mismatch_is_refused);
    RUN_TEST(test_header_errors_are_typed);
    RUN_TEST(test_row_errors_are_typed);
    return UNITY_END();
}
