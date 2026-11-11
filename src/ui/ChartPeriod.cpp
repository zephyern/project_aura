// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/ChartPeriod.h"

#include <stdio.h>
#include <string.h>

#include "config/AppConfig.h"
#include "core/LocalDate.h"

namespace ChartPeriod {
namespace {

constexpr const char *kMonthToken = "{month}";
constexpr const char *kYearToken = "{year}";
constexpr int32_t kTmYearBase = 1900;

// Local midnight on day 1 of `month` (1..12) of `year`, in UTC seconds.
bool localMonthStart(int32_t year, int32_t month, uint32_t &out) {
    tm local{};
    local.tm_year = year - kTmYearBase;
    local.tm_mon = month - 1;
    local.tm_mday = 1;
    local.tm_isdst = -1; // let the zone rules decide
    const time_t epoch = mktime(&local);
    if (epoch == static_cast<time_t>(-1) || epoch <= 0 || static_cast<uint64_t>(epoch) > UINT32_MAX) return false;
    out = static_cast<uint32_t>(epoch);
    return true;
}

bool trustedLocal(time_t epoch, tm &out) {
    return epoch > static_cast<time_t>(Config::TIME_VALID_EPOCH) && LocalDate::localtimeInto(epoch, out);
}

bool findOnce(const char *text, const char *token, const char *&at) {
    at = strstr(text, token);
    return at && !strstr(at + strlen(token), token);
}

} // namespace

Navigator make(int32_t oldest, int32_t latest, int32_t current) {
    Navigator nav;
    nav.latest = latest;
    nav.oldest = oldest > latest ? latest : oldest;
    nav.current = current < nav.oldest ? nav.oldest : (current > nav.latest ? nav.latest : current);
    return nav;
}

bool canStep(const Navigator &nav, Step step) {
    return step == Step::Back ? nav.current > nav.oldest : nav.current < nav.latest;
}

bool step(Navigator &nav, Step step) {
    if (!canStep(nav, step)) return false;
    nav.current += step == Step::Back ? -1 : 1;
    return true;
}

bool localMonthBounds(int32_t month_index, uint32_t &start, uint32_t &end) {
    return localMonthStart(monthYear(month_index), monthOfYear(month_index), start) &&
           localMonthStart(monthYear(month_index + 1), monthOfYear(month_index + 1), end) && end > start;
}

bool localYearBounds(int32_t year, uint32_t &start, uint32_t &end) {
    return localMonthStart(year, 1, start) && localMonthStart(year + 1, 1, end) && end > start;
}

bool localMonthIndexAt(time_t epoch, int32_t &month_index) {
    tm local{};
    if (!trustedLocal(epoch, local)) return false;
    month_index = monthIndex(local.tm_year + kTmYearBase, local.tm_mon + 1);
    return true;
}

bool localYearAt(time_t epoch, int32_t &year) {
    tm local{};
    if (!trustedLocal(epoch, local)) return false;
    year = local.tm_year + kTmYearBase;
    return true;
}

bool formatTitle(const char *title_template, const char *month_name, int32_t year, char *out, size_t len) {
    if (!title_template || !month_name || !out || len == 0) return false;
    const char *month_at = nullptr;
    const char *year_at = nullptr;
    if (!findOnce(title_template, kMonthToken, month_at) || !findOnce(title_template, kYearToken, year_at)) {
        return false;
    }
    char year_text[12] = {};
    snprintf(year_text, sizeof(year_text), "%ld", static_cast<long>(year));
    size_t used = 0;
    const char *p = title_template;
    while (*p) {
        const char *piece = nullptr;
        size_t skip = 1;
        if (p == month_at) {
            piece = month_name;
            skip = strlen(kMonthToken);
        } else if (p == year_at) {
            piece = year_text;
            skip = strlen(kYearToken);
        }
        const size_t piece_len = piece ? strlen(piece) : 1;
        if (used + piece_len >= len) return false;
        memcpy(out + used, piece ? piece : p, piece_len);
        used += piece_len;
        p += skip;
    }
    out[used] = '\0';
    return true;
}

} // namespace ChartPeriod
