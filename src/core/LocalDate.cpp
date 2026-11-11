// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/LocalDate.h"

#include <stdio.h>

#include "config/AppConfig.h"

namespace LocalDate {
namespace {

constexpr int kFirstValidYear = 2020;

bool dayKeyFrom(const tm &t, uint32_t &day_key) {
    const int year = t.tm_year + 1900;
    const int month = t.tm_mon + 1;
    const int day = t.tm_mday;
    if (year < kFirstValidYear || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    day_key = static_cast<uint32_t>(year * 10000 + month * 100 + day);
    return true;
}

} // namespace

bool localtimeInto(time_t epoch, tm &out) {
#if defined(_WIN32)
    return localtime_s(&out, &epoch) == 0;
#else
    return localtime_r(&epoch, &out) != nullptr;
#endif
}

bool gmtimeInto(time_t epoch, tm &out) {
#if defined(_WIN32)
    return gmtime_s(&out, &epoch) == 0;
#else
    return gmtime_r(&epoch, &out) != nullptr;
#endif
}

bool localDayKey(time_t epoch, uint32_t &day_key) {
    tm t = {};
    return epoch > Config::TIME_VALID_EPOCH && localtimeInto(epoch, t) && dayKeyFrom(t, day_key);
}

bool utcDayKey(time_t epoch, uint32_t &day_key) {
    tm t = {};
    return epoch > Config::TIME_VALID_EPOCH && gmtimeInto(epoch, t) && dayKeyFrom(t, day_key);
}

void formatDayKey(uint32_t day_key, char *out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%04u-%02u-%02u",
             static_cast<unsigned>(day_key / 10000U),
             static_cast<unsigned>((day_key / 100U) % 100U),
             static_cast<unsigned>(day_key % 100U));
}

void formatLocalTime(time_t epoch, char *out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    tm t = {};
    if (!localtimeInto(epoch, t)) {
        out[0] = '\0';
        return;
    }
    snprintf(out, len, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
}

} // namespace LocalDate
