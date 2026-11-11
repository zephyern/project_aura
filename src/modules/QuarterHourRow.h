// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "modules/ChartsHistory.h"
#include "modules/HistoryAggregate.h"

// One CSV row per UTC quarter hour of long-term history (docs/LONG_HISTORY.md).
//
// Header: start_utc_v1, then per metric (ChartsHistory::Metric order, keys from
// DailyExtremaHistory::metricDef) <key>_sum,<key>_count,<key>_min,<key>_min_s,
// <key>_max,<key>_max_s, then optional_gas_type.
// Row: quarter start (UTC epoch seconds, a multiple of 900), then per metric the
// exact sum (%.17g), the count, the low and high (%.9g) and the second into the
// quarter (0-899) at which each was first reached. A metric without samples has
// count 0 and its other five fields empty. Only numbers appear, so the format
// needs no quoting or escaping; '.' is the decimal separator.
//
// Summary files (hourly, daily, weekly) use the span layout: the same columns
// with end_utc (exclusive UTC end) after the start, any start and length, and
// low/high seconds counted from the start.
namespace QuarterHourRow {

constexpr uint32_t kQuarterSeconds = 900;
constexpr const char *kStartColumn = "start_utc_v1";
constexpr const char *kOptionalGasTypeColumn = "optional_gas_type";
constexpr size_t kFieldsPerMetric = 6;
constexpr size_t kFieldCount = 1 + ChartsHistory::kMetricCount * kFieldsPerMetric + 1;
constexpr const char *kEndColumn = "end_utc";
constexpr size_t kSpanFieldCount = kFieldCount + 1;

struct Row {
    uint32_t start_epoch = 0;
    // For METRIC_OPTIONAL_GAS, source is the optional gas type; 0 elsewhere.
    HistoryAggregate metrics[ChartsHistory::kMetricCount] = {};
};

struct SpanRow {
    uint32_t start_epoch = 0;
    uint32_t end_epoch = 0; // exclusive
    HistoryAggregate metrics[ChartsHistory::kMetricCount] = {};
};

enum class EncodeResult : uint8_t {
    Ok,
    BadStart,       // zero or not a quarter-hour boundary (span: zero, or end <= start)
    NonFinite,      // sum, low or high is NaN/inf
    TimeOutside,    // low/high time outside [start, end)
    BadSource,      // source on a non-gas metric, or gas data without a type
};

enum class DecodeResult : uint8_t {
    Ok,
    UnsupportedVersion, // first column is start_utc_v<N> for another N
    BadHeader,      // wrong first column, or columns missing/out of order
    UnknownColumn,  // a column name this firmware does not know
    Malformed,      // wrong field count or a field that is not a plain number
    NonFinite,
    BadValue,       // start, count, low/high, seconds or gas type out of range
};

// Appends the header line, including '\n'.
void appendHeader(String &out);
// Appends one row, including '\n'. `out` is unchanged unless Ok.
EncodeResult appendRow(const Row &row, String &out);

// `line` excludes the trailing '\n'.
DecodeResult decodeHeader(const char *line, size_t len);
DecodeResult decodeRow(const char *line, size_t len, Row &out);

// The same for summary files (span layout).
void appendSpanHeader(String &out);
EncodeResult appendSpanRow(const SpanRow &row, String &out);
DecodeResult decodeSpanHeader(const char *line, size_t len);
DecodeResult decodeSpanRow(const char *line, size_t len, SpanRow &out);

} // namespace QuarterHourRow
