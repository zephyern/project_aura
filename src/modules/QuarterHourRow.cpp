// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/QuarterHourRow.h"

#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules/DailyExtremaHistory.h"

namespace QuarterHourRow {
namespace {

constexpr const char *kSuffixes[kFieldsPerMetric] = {
    "_sum", "_count", "_min", "_min_s", "_max", "_max_s",
};
constexpr size_t kMaxNumberChars = 40;
constexpr int kOptionalGas = ChartsHistory::METRIC_OPTIONAL_GAS;
constexpr int kOutdoor = ChartsHistory::METRIC_OUTDOOR_TEMP;
enum class Layout : uint8_t { Quarter, Span };
// Every version of the first column starts with this; the suffix is the version.
constexpr const char *kStartPrefix = "start_utc_v";

struct Field {
    const char *data = nullptr;
    size_t len = 0;
};

void appendDouble(String &out, double value) {
    char buf[kMaxNumberChars];
    snprintf(buf, sizeof(buf), "%.17g", value);
    out += buf;
}

void appendFloat(String &out, float value) {
    char buf[kMaxNumberChars];
    snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(value));
    out += buf;
}

void appendUnsigned(String &out, uint32_t value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(value));
    out += buf;
}

bool fieldEquals(const Field &field, const char *prefix, const char *suffix) {
    const size_t prefix_len = strlen(prefix);
    const size_t suffix_len = suffix ? strlen(suffix) : 0;
    return field.len == prefix_len + suffix_len &&
           memcmp(field.data, prefix, prefix_len) == 0 &&
           (suffix_len == 0 || memcmp(field.data + prefix_len, suffix, suffix_len) == 0);
}

bool knownColumn(const Field &field) {
    if (fieldEquals(field, kStartColumn, nullptr) || fieldEquals(field, kEndColumn, nullptr) ||
        fieldEquals(field, kOptionalGasTypeColumn, nullptr) || fieldEquals(field, kOutdoorSourceColumn, nullptr)) {
        return true;
    }
    for (uint8_t m = 0; m < ChartsHistory::kMetricCount; ++m) {
        for (const char *suffix : kSuffixes) {
            if (fieldEquals(field, DailyExtremaHistory::metricDef(m).key, suffix)) {
                return true;
            }
        }
    }
    return false;
}

// Splits exactly `expected` comma-separated fields. Any other count, or a
// quote/CR/control character anywhere, is malformed.
bool splitFields(const char *line, size_t len, Field *fields, size_t expected) {
    if (!line) {
        return false;
    }
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i < len) {
            const unsigned char ch = static_cast<unsigned char>(line[i]);
            if (ch == '"' || ch < 0x20 || ch == 0x7f) {
                return false;
            }
            if (ch != ',') {
                continue;
            }
        }
        if (count == expected) {
            return false;
        }
        fields[count].data = line + start;
        fields[count].len = i - start;
        ++count;
        start = i + 1;
    }
    return count == expected;
}

bool parseUnsigned(const Field &field, uint32_t max_value, uint32_t &out) {
    if (field.len == 0 || field.len > 10 || (field.len > 1 && field.data[0] == '0')) {
        return false;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < field.len; ++i) {
        const char ch = field.data[i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10U + static_cast<uint64_t>(ch - '0');
    }
    if (value > max_value) {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

// Plain decimal or exponent notation only: no spaces, '+' prefix, hex, inf or nan.
bool copyNumber(const Field &field, char (&buf)[kMaxNumberChars]) {
    if (field.len == 0 || field.len >= kMaxNumberChars) {
        return false;
    }
    const char first = field.data[0];
    if (first != '-' && (first < '0' || first > '9')) {
        return false;
    }
    for (size_t i = 0; i < field.len; ++i) {
        const char ch = field.data[i];
        if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' ||
              ch == 'e' || ch == 'E')) {
            return false;
        }
    }
    memcpy(buf, field.data, field.len);
    buf[field.len] = '\0';
    return true;
}

DecodeResult parseDouble(const Field &field, double &out) {
    char buf[kMaxNumberChars];
    if (!copyNumber(field, buf)) {
        return DecodeResult::Malformed;
    }
    char *end = nullptr;
    const double value = strtod(buf, &end);
    if (end != buf + field.len) {
        return DecodeResult::Malformed;
    }
    if (!std::isfinite(value)) {
        return DecodeResult::NonFinite;
    }
    out = value;
    return DecodeResult::Ok;
}

DecodeResult parseFloat(const Field &field, float &out) {
    char buf[kMaxNumberChars];
    if (!copyNumber(field, buf)) {
        return DecodeResult::Malformed;
    }
    char *end = nullptr;
    const float value = strtof(buf, &end);
    if (end != buf + field.len) {
        return DecodeResult::Malformed;
    }
    if (!std::isfinite(value)) {
        return DecodeResult::NonFinite;
    }
    out = value;
    return DecodeResult::Ok;
}

DecodeResult decodeMetric(const Field *f, uint32_t start, uint32_t length, HistoryAggregate &out) {
    out = HistoryAggregate{};
    uint32_t count = 0;
    if (!parseUnsigned(f[1], UINT32_MAX, count)) {
        return DecodeResult::Malformed;
    }
    if (count == 0) {
        for (size_t i = 0; i < kFieldsPerMetric; ++i) {
            if (i != 1 && f[i].len != 0) {
                return DecodeResult::Malformed;
            }
        }
        return DecodeResult::Ok;
    }
    HistoryAggregate a;
    a.count = count;
    uint32_t min_s = 0, max_s = 0;
    DecodeResult result = parseDouble(f[0], a.sum);
    if (result == DecodeResult::Ok) result = parseFloat(f[2], a.min);
    if (result == DecodeResult::Ok) result = parseFloat(f[4], a.max);
    if (result != DecodeResult::Ok) {
        return result;
    }
    if (!parseUnsigned(f[3], length - 1, min_s) ||
        !parseUnsigned(f[5], length - 1, max_s)) {
        return DecodeResult::BadValue;
    }
    if (a.min > a.max) {
        return DecodeResult::BadValue;
    }
    a.min_epoch = start + min_s;
    a.max_epoch = start + max_s;
    out = a;
    return DecodeResult::Ok;
}

// Columns before the metrics: the start, and for spans the end.
size_t leadingColumns(Layout layout) { return layout == Layout::Span ? 2 : 1; }

size_t fieldCount(Layout layout) {
    return leadingColumns(layout) + ChartsHistory::kMetricCount * kFieldsPerMetric + 2;
}

void appendHeaderFor(Layout layout, String &out) {
    String line = kStartColumn;
    if (layout == Layout::Span) {
        line += ',';
        line += kEndColumn;
    }
    for (uint8_t m = 0; m < ChartsHistory::kMetricCount; ++m) {
        for (const char *suffix : kSuffixes) {
            line += ',';
            line += DailyExtremaHistory::metricDef(m).key;
            line += suffix;
        }
    }
    line += ',';
    line += kOptionalGasTypeColumn;
    line += ',';
    line += kOutdoorSourceColumn;
    line += '\n';
    out += line;
}

EncodeResult appendRowFor(Layout layout, uint32_t start, uint32_t end,
                          const HistoryAggregate (&metrics)[ChartsHistory::kMetricCount], String &out) {
    String line;
    appendUnsigned(line, start);
    if (layout == Layout::Span) {
        line += ',';
        appendUnsigned(line, end);
    }
    for (int m = 0; m < ChartsHistory::kMetricCount; ++m) {
        const HistoryAggregate &a = metrics[m];
        if (m != kOptionalGas && m != kOutdoor && a.source != 0) {
            return EncodeResult::BadSource;
        }
        if (a.empty()) {
            if (m == kOutdoor && a.source != 0) return EncodeResult::BadSource;
            line += ",,0,,,,";
            continue;
        }
        if ((m == kOptionalGas && a.source == 0) ||
            (m == kOutdoor && a.source != ChartsHistory::OUTDOOR_SOURCE_API)) {
            return EncodeResult::BadSource;
        }
        if (!std::isfinite(a.sum) || !std::isfinite(a.min) || !std::isfinite(a.max)) {
            return EncodeResult::NonFinite;
        }
        if (a.min_epoch < start || a.min_epoch >= end || a.max_epoch < start || a.max_epoch >= end) {
            return EncodeResult::TimeOutside;
        }
        line += ',';
        appendDouble(line, a.sum);
        line += ',';
        appendUnsigned(line, a.count);
        line += ',';
        appendFloat(line, a.min);
        line += ',';
        appendUnsigned(line, a.min_epoch - start);
        line += ',';
        appendFloat(line, a.max);
        line += ',';
        appendUnsigned(line, a.max_epoch - start);
    }
    line += ',';
    appendUnsigned(line, metrics[kOptionalGas].source);
    line += ',';
    if (!metrics[kOutdoor].empty()) line += kOutdoorSourceApi;
    line += '\n';
    out += line;
    return EncodeResult::Ok;
}

DecodeResult decodeHeaderFor(Layout layout, const char *line, size_t len) {
    Field fields[kSpanFieldCount];
    if (!line) {
        return DecodeResult::BadHeader;
    }
    // Check the version before the column count: another version may have
    // a different set of columns.
    const char *comma = static_cast<const char *>(memchr(line, ',', len));
    const Field first{line, comma ? static_cast<size_t>(comma - line) : len};
    const size_t prefix_len = strlen(kStartPrefix);
    if (first.len > prefix_len && memcmp(first.data, kStartPrefix, prefix_len) == 0 &&
        !fieldEquals(first, kStartColumn, nullptr)) {
        return DecodeResult::UnsupportedVersion;
    }
    const size_t count = fieldCount(layout);
    if (!splitFields(line, len, fields, count)) {
        return DecodeResult::BadHeader;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!knownColumn(fields[i])) {
            return DecodeResult::UnknownColumn;
        }
    }
    const size_t lead = leadingColumns(layout);
    if (!fieldEquals(fields[0], kStartColumn, nullptr) ||
        (layout == Layout::Span && !fieldEquals(fields[1], kEndColumn, nullptr)) ||
        !fieldEquals(fields[count - 2], kOptionalGasTypeColumn, nullptr) ||
        !fieldEquals(fields[count - 1], kOutdoorSourceColumn, nullptr)) {
        return DecodeResult::BadHeader;
    }
    for (uint8_t m = 0; m < ChartsHistory::kMetricCount; ++m) {
        for (size_t i = 0; i < kFieldsPerMetric; ++i) {
            if (!fieldEquals(fields[lead + m * kFieldsPerMetric + i],
                             DailyExtremaHistory::metricDef(m).key, kSuffixes[i])) {
                return DecodeResult::BadHeader;
            }
        }
    }
    return DecodeResult::Ok;
}

DecodeResult decodeRowFor(Layout layout, const char *line, size_t len, uint32_t &start_out,
                          uint32_t &end_out, HistoryAggregate (&metrics_out)[ChartsHistory::kMetricCount]) {
    Field fields[kSpanFieldCount];
    const size_t count = fieldCount(layout);
    if (!splitFields(line, len, fields, count)) {
        return DecodeResult::Malformed;
    }
    uint32_t start = 0;
    uint32_t end = 0;
    if (!parseUnsigned(fields[0], UINT32_MAX - 1, start)) {
        return DecodeResult::Malformed;
    }
    if (layout == Layout::Quarter) {
        if (start == 0 || start % kQuarterSeconds != 0 || start > UINT32_MAX - kQuarterSeconds) {
            return DecodeResult::BadValue;
        }
        end = start + kQuarterSeconds;
    } else {
        if (!parseUnsigned(fields[1], UINT32_MAX, end)) {
            return DecodeResult::Malformed;
        }
        if (start == 0 || end <= start) {
            return DecodeResult::BadValue;
        }
    }
    const size_t lead = leadingColumns(layout);
    HistoryAggregate metrics[ChartsHistory::kMetricCount];
    for (int m = 0; m < ChartsHistory::kMetricCount; ++m) {
        const DecodeResult result =
            decodeMetric(&fields[lead + m * kFieldsPerMetric], start, end - start, metrics[m]);
        if (result != DecodeResult::Ok) {
            return result;
        }
    }
    uint32_t gas_type = 0;
    if (!parseUnsigned(fields[count - 2], UINT8_MAX, gas_type)) {
        return DecodeResult::Malformed;
    }
    HistoryAggregate &gas = metrics[kOptionalGas];
    if (!gas.empty() && gas_type == 0) {
        return DecodeResult::BadValue;
    }
    gas.source = static_cast<uint8_t>(gas_type);
    // The outdoor source word must match the data: "api" with data, empty without.
    HistoryAggregate &outdoor = metrics[kOutdoor];
    const Field &word = fields[count - 1];
    if (outdoor.empty() ? word.len != 0 : !fieldEquals(word, kOutdoorSourceApi, nullptr)) {
        return DecodeResult::BadValue;
    }
    outdoor.source = outdoor.empty() ? ChartsHistory::OUTDOOR_SOURCE_NONE : ChartsHistory::OUTDOOR_SOURCE_API;
    start_out = start;
    end_out = end;
    for (int m = 0; m < ChartsHistory::kMetricCount; ++m) metrics_out[m] = metrics[m];
    return DecodeResult::Ok;
}

} // namespace

void appendHeader(String &out) { appendHeaderFor(Layout::Quarter, out); }

void appendSpanHeader(String &out) { appendHeaderFor(Layout::Span, out); }

EncodeResult appendRow(const Row &row, String &out) {
    if (row.start_epoch == 0 || row.start_epoch % kQuarterSeconds != 0 ||
        row.start_epoch > UINT32_MAX - kQuarterSeconds) {
        return EncodeResult::BadStart;
    }
    return appendRowFor(Layout::Quarter, row.start_epoch, row.start_epoch + kQuarterSeconds,
                        row.metrics, out);
}

EncodeResult appendSpanRow(const SpanRow &row, String &out) {
    if (row.start_epoch == 0 || row.end_epoch <= row.start_epoch) {
        return EncodeResult::BadStart;
    }
    return appendRowFor(Layout::Span, row.start_epoch, row.end_epoch, row.metrics, out);
}

DecodeResult decodeHeader(const char *line, size_t len) {
    return decodeHeaderFor(Layout::Quarter, line, len);
}

DecodeResult decodeSpanHeader(const char *line, size_t len) {
    return decodeHeaderFor(Layout::Span, line, len);
}

DecodeResult decodeRow(const char *line, size_t len, Row &out) {
    Row row;
    uint32_t end = 0;
    const DecodeResult r = decodeRowFor(Layout::Quarter, line, len, row.start_epoch, end, row.metrics);
    if (r == DecodeResult::Ok) out = row;
    return r;
}

DecodeResult decodeSpanRow(const char *line, size_t len, SpanRow &out) {
    SpanRow row;
    const DecodeResult r =
        decodeRowFor(Layout::Span, line, len, row.start_epoch, row.end_epoch, row.metrics);
    if (r == DecodeResult::Ok) out = row;
    return r;
}

} // namespace QuarterHourRow
