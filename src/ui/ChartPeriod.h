// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// The calendar periods the long chart ranges show (docs/CHARTS_HISTORY.md):
// one local month or one local year, stepped with the ‹ › arrows. A period is
// a single integer (a month index or a year), so stepping and its limits are
// plain integer arithmetic.
namespace ChartPeriod {

// Month index: year * 12 + (month - 1), month 1..12.
constexpr int32_t monthIndex(int32_t year, int32_t month) { return year * 12 + (month - 1); }
constexpr int32_t monthYear(int32_t index) { return index / 12; }
constexpr int32_t monthOfYear(int32_t index) { return index % 12 + 1; }

// Day key YYYYMMDD -> month index / year.
constexpr int32_t monthIndexOfDayKey(uint32_t day_key) {
    return monthIndex(static_cast<int32_t>(day_key / 10000U), static_cast<int32_t>((day_key / 100U) % 100U));
}
constexpr int32_t yearOfDayKey(uint32_t day_key) { return static_cast<int32_t>(day_key / 10000U); }

// The period on show and how far the arrows may go: back to the oldest period
// with data, forward to the current one (never into the future).
struct Navigator {
    int32_t oldest = 0;
    int32_t latest = 0;
    int32_t current = 0;
};
enum class Step : uint8_t { Back, Forward };

// A navigator showing `current`, kept inside [oldest, latest]. An oldest
// after latest (data dated in the future) is treated as latest alone.
Navigator make(int32_t oldest, int32_t latest, int32_t current);
bool canStep(const Navigator &nav, Step step);
// Moves one period; false (unchanged) at the limit.
bool step(Navigator &nav, Step step);

// [start, end) in UTC seconds of a local month / local year, from local
// midnight on its first day to local midnight on the first day of the next.
// False for a period mktime cannot represent.
bool localMonthBounds(int32_t month_index, uint32_t &start, uint32_t &end);
bool localYearBounds(int32_t year, uint32_t &start, uint32_t &end);

// The local month index / year of `epoch`; false unless the time is trusted
// (after Config::TIME_VALID_EPOCH).
bool localMonthIndexAt(time_t epoch, int32_t &month_index);
bool localYearAt(time_t epoch, int32_t &year);

// Fills a title template: "{month}" becomes `month_name` and "{year}" the
// year, each exactly once (e.g. "{month} {year}" -> "Oct 2026",
// "{year}年{month}" -> "2026年10月"). False if a token is missing or repeated
// or the result does not fit.
bool formatTitle(const char *title_template, const char *month_name, int32_t year, char *out, size_t len);

} // namespace ChartPeriod
