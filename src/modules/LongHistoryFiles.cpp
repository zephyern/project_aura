// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/LongHistoryFiles.h"

#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>

#include "core/LocalDate.h"
#include "core/Logger.h"

namespace LongHistoryFiles {
namespace {

constexpr const char *kTag = "History";
constexpr size_t kTailChunk = 256;
constexpr size_t kReadChunk = 4096;
constexpr uint32_t kFirstYear = 2020;
constexpr uint32_t kLastYear = 2099;

bool digits(const char *text, size_t count, uint32_t &out) {
    uint32_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10U + static_cast<uint32_t>(text[i] - '0');
    }
    out = value;
    return true;
}

bool validDayKey(uint32_t key) {
    static constexpr uint8_t kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint32_t year = key / 10000U, month = (key / 100U) % 100U, day = key % 100U;
    if (year < kFirstYear || year > kLastYear || month < 1 || month > 12 || day < 1) return false;
    const bool leap = (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
    const uint32_t max_day = month == 2 && !leap ? 28U : kDays[month - 1];
    return day <= max_day;
}

// Newest entry whose name parses, tracked across listDir callbacks.
struct Newest {
    enum Kind : uint8_t { Year, Month, Day } kind;
    uint32_t year = 0;
    uint32_t month = 0;
    uint32_t best = 0;
};

bool takeNewest(const char *name, bool is_dir, void *context) {
    Newest &n = *static_cast<Newest *>(context);
    const size_t len = strlen(name);
    uint32_t value = 0;
    if (n.kind == Newest::Year) {
        if (!is_dir || len != 4 || !digits(name, 4, value) || value < kFirstYear || value > kLastYear)
            return true;
    } else if (n.kind == Newest::Month) {
        if (!is_dir || len != 2 || !digits(name, 2, value) || value < 1 || value > 12) return true;
    } else {
        // YYYY-MM-DD.csv inside YYYY/MM
        uint32_t y = 0, m = 0, d = 0;
        if (is_dir || len != 14 || name[4] != '-' || name[7] != '-' ||
            strcmp(name + 10, ".csv") != 0 || !digits(name, 4, y) || !digits(name + 5, 2, m) ||
            !digits(name + 8, 2, d) || y != n.year || m != n.month) {
            return true;
        }
        value = y * 10000U + m * 100U + d;
        if (!validDayKey(value)) return true;
    }
    if (value > n.best) n.best = value;
    return true;
}

// Lists `path` for the newest entry of `kind`. Failed only on storage failure.
FindResult newestIn(const DailyHistoryStorage &storage, const char *path, Newest &n) {
    n.best = 0;
    const DirListResult listed = storage.listDir(path, &takeNewest, &n);
    if (listed == DirListResult::Failed) return FindResult::Failed;
    return n.best ? FindResult::Found : FindResult::None;
}

const char *decodeReason(QuarterHourRow::DecodeResult r) {
    using QuarterHourRow::DecodeResult;
    switch (r) {
        case DecodeResult::Ok: return "ok";
        case DecodeResult::UnsupportedVersion: return "unsupported version";
        case DecodeResult::BadHeader: return "bad header";
        case DecodeResult::UnknownColumn: return "unknown column";
        case DecodeResult::Malformed: return "malformed";
        case DecodeResult::NonFinite: return "non-finite number";
        case DecodeResult::BadValue: return "value out of range";
    }
    return "unknown";
}

} // namespace

bool quarterFilePath(uint32_t utc_day_key, char (&out)[kPathCapacity]) {
    if (!validDayKey(utc_day_key)) return false;
    const unsigned y = utc_day_key / 10000U, m = (utc_day_key / 100U) % 100U, d = utc_day_key % 100U;
    snprintf(out, sizeof(out), "%s/%04u/%02u/%04u-%02u-%02u.csv", kQuarterRoot, y, m, y, m, d);
    return true;
}

FindResult findNewestQuarterFile(const DailyHistoryStorage &storage, uint32_t &utc_day_key) {
    // Walk years newest first; a year or month directory may be empty after a
    // partial clear, so keep looking further back.
    Newest years{Newest::Year};
    FindResult r = newestIn(storage, kQuarterRoot, years);
    if (r != FindResult::Found) return r;
    for (uint32_t year = years.best; year >= kFirstYear; --year) {
        char year_path[kPathCapacity];
        snprintf(year_path, sizeof(year_path), "%s/%04u", kQuarterRoot, static_cast<unsigned>(year));
        Newest months{Newest::Month};
        r = newestIn(storage, year_path, months);
        if (r == FindResult::Failed) return r;
        if (r == FindResult::None) continue;
        for (uint32_t month = months.best; month >= 1; --month) {
            char month_path[kPathCapacity];
            snprintf(month_path, sizeof(month_path), "%s/%04u/%02u", kQuarterRoot, static_cast<unsigned>(year),
                     static_cast<unsigned>(month));
            Newest days{Newest::Day};
            days.year = year;
            days.month = month;
            r = newestIn(storage, month_path, days);
            if (r == FindResult::Failed) return r;
            if (r == FindResult::Found) {
                utc_day_key = days.best;
                return FindResult::Found;
            }
        }
    }
    return FindResult::None;
}

RepairResult repairTail(DailyHistoryStorage &storage, const char *path, const char *header,
                        size_t &removed_bytes) {
    removed_bytes = 0;
    if (!path || !header) return RepairResult::Failed;
    bool exists = false;
    size_t size = 0;
    if (!storage.fileInfo(path, exists, size)) return RepairResult::Failed;
    if (!exists) return RepairResult::Absent;
    if (size == 0) return RepairResult::Clean;

    // Scan backwards from the end for the last '\n'.
    char chunk[kTailChunk];
    size_t end = size;
    size_t keep = 0;
    bool found = false;
    while (end > 0 && !found) {
        const size_t start = end > sizeof(chunk) ? end - sizeof(chunk) : 0;
        size_t got = 0;
        if (!storage.readRange(path, start, chunk, end - start, got) || got != end - start) {
            return RepairResult::Failed;
        }
        if (end == size && chunk[got - 1] == '\n') return RepairResult::Clean;
        for (size_t i = got; i > 0; --i) {
            if (chunk[i - 1] == '\n') {
                keep = start + i;
                found = true;
                break;
            }
        }
        end = start;
    }
    removed_bytes = size - keep;
    if (!found) {
        // Not even the header was completed: start the file again.
        if (!storage.writeBinaryAtomic(path, header, strlen(header))) return RepairResult::Failed;
        LOGW(kTag, "repaired %s: torn header (%u bytes) rewritten", path,
             static_cast<unsigned>(removed_bytes));
        return RepairResult::HeaderRewritten;
    }
    if (!storage.truncateAtomic(path, keep)) return RepairResult::Failed;
    LOGW(kTag, "repaired %s: removed %u bytes after the last complete line", path,
         static_cast<unsigned>(removed_bytes));
    return RepairResult::Repaired;
}

namespace {

// The one line reader behind every long-term file: header check, then each
// decodable row in order; bad lines skipped and logged.
template <typename RowT, typename HeaderDecode, typename RowDecode, typename Deliver>
ReadResult readFile(const DailyHistoryStorage &storage, const char *path, ReadStats &stats,
                    HeaderDecode decode_header, RowDecode decode_row, Deliver deliver) {
    stats = ReadStats{};
    if (!path) return ReadResult::Failed;
    bool exists = false;
    size_t size = 0;
    if (!storage.fileInfo(path, exists, size)) return ReadResult::Failed;
    if (!exists) return ReadResult::Absent;
    if (size == 0) return ReadResult::Empty;

    std::unique_ptr<char[]> chunk(new (std::nothrow) char[kReadChunk]);
    std::unique_ptr<char[]> line(new (std::nothrow) char[kMaxLineBytes]);
    if (!chunk || !line) {
        LOGE(kTag, "no memory to read %s", path);
        return ReadResult::Failed;
    }
    size_t line_len = 0;
    size_t line_start = 0;
    bool overlong = false;
    uint32_t line_no = 0;
    bool header_done = false;
    bool stop = false;

    // Returns false if reading must end (bad header or caller stop).
    const auto finish_line = [&](bool terminated) -> bool {
        ++line_no;
        if (!header_done) {
            header_done = true;
            if (overlong || !terminated ||
                decode_header(line.get(), line_len) != QuarterHourRow::DecodeResult::Ok) {
                LOGW(kTag, "%s: unsupported header, file not read", path);
                return false;
            }
            return true;
        }
        RowT row;
        QuarterHourRow::DecodeResult decoded = QuarterHourRow::DecodeResult::Malformed;
        if (!overlong) decoded = decode_row(line.get(), line_len, row);
        if (!terminated || decoded != QuarterHourRow::DecodeResult::Ok) {
            ++stats.skipped;
            LOGW(kTag, "%s: skipped line %u (%s)", path, static_cast<unsigned>(line_no),
                 !terminated ? "unterminated" : overlong ? "too long" : decodeReason(decoded));
            return true;
        }
        ++stats.rows;
        if (!deliver(row, line_start)) stop = true;
        return !stop;
    };

    size_t offset = 0;
    while (offset < size) {
        size_t got = 0;
        const size_t want = size - offset < kReadChunk ? size - offset : kReadChunk;
        if (!storage.readRange(path, offset, chunk.get(), want, got) || got == 0) {
            return ReadResult::Failed;
        }
        for (size_t i = 0; i < got; ++i) {
            const char ch = chunk[i];
            if (ch == '\n') {
                if (!finish_line(true)) return stop ? ReadResult::Read : ReadResult::BadHeader;
                line_len = 0;
                line_start = offset + i + 1;
                overlong = false;
                continue;
            }
            if (line_len < kMaxLineBytes) {
                line[line_len++] = ch;
            } else {
                overlong = true;
            }
        }
        offset += got;
    }
    if (line_len > 0 || overlong) {
        if (!finish_line(false)) return stop ? ReadResult::Read : ReadResult::BadHeader;
    }
    return header_done ? ReadResult::Read : ReadResult::BadHeader;
}

} // namespace

ReadResult readQuarterFile(const DailyHistoryStorage &storage, const char *path, RowFn fn,
                           void *context, ReadStats &stats) {
    if (!fn) return ReadResult::Failed;
    return readFile<QuarterHourRow::Row>(
        storage, path, stats, &QuarterHourRow::decodeHeader, &QuarterHourRow::decodeRow,
        [&](const QuarterHourRow::Row &row, size_t offset) { return fn(row, offset, context); });
}

ReadResult readSpanFile(const DailyHistoryStorage &storage, const char *path, SpanFn fn,
                        void *context, ReadStats &stats) {
    if (!fn) return ReadResult::Failed;
    return readFile<QuarterHourRow::SpanRow>(
        storage, path, stats, &QuarterHourRow::decodeSpanHeader, &QuarterHourRow::decodeSpanRow,
        [&](const QuarterHourRow::SpanRow &row, size_t offset) { return fn(row, offset, context); });
}

namespace {
struct RangeFilter {
    uint32_t from = 0;
    uint32_t to = 0;
    RowFn fn = nullptr;
    void *context = nullptr;
    bool stopped = false;
};

bool filterRange(const QuarterHourRow::Row &row, size_t offset, void *context) {
    RangeFilter &f = *static_cast<RangeFilter *>(context);
    if (row.start_epoch < f.from || row.start_epoch >= f.to) return true;
    if (!f.fn(row, offset, f.context)) {
        f.stopped = true;
        return false;
    }
    return true;
}

constexpr uint32_t kDaySeconds = 86400;
} // namespace

ScanResult scanQuarterRows(const DailyHistoryStorage &storage, uint32_t from, uint32_t to, RowFn fn,
                           void *context, ScanStats &stats) {
    stats = ScanStats{};
    if (!fn || to <= from) return ScanResult::Done;
    RangeFilter filter{from, to, fn, context, false};
    for (uint32_t day = from - from % kDaySeconds; day < to; day += kDaySeconds) {
        uint32_t key = 0;
        char path[kPathCapacity];
        if (!LocalDate::utcDayKey(day, key) || !quarterFilePath(key, path)) continue;
        ReadStats rs;
        switch (readQuarterFile(storage, path, &filterRange, &filter, rs)) {
            case ReadResult::Read:
                ++stats.files_read;
                stats.rows_skipped += rs.skipped;
                break;
            case ReadResult::Absent:
            case ReadResult::Empty:
                break;
            case ReadResult::BadHeader:
                ++stats.files_unsupported; // logged by the reader
                break;
            case ReadResult::Failed:
                return ScanResult::Failed;
        }
        if (filter.stopped) return ScanResult::Stopped;
        if (day > UINT32_MAX - kDaySeconds) break;
    }
    return ScanResult::Done;
}

bool hourlyFilePath(uint32_t utc_year, uint32_t utc_month, char (&out)[kPathCapacity]) {
    if (utc_year < kFirstYear || utc_year > kLastYear || utc_month < 1 || utc_month > 12) return false;
    snprintf(out, sizeof(out), "%s/%04u-%02u.csv", kHourlyRoot, static_cast<unsigned>(utc_year),
             static_cast<unsigned>(utc_month));
    return true;
}

} // namespace LongHistoryFiles
