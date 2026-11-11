// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Calendar helpers shared by every history module, so that day boundaries,
// day keys and printed times are computed one way only.
namespace LocalDate {

// Thread-safe localtime/gmtime. False if the conversion fails.
bool localtimeInto(time_t epoch, tm &out);
bool gmtimeInto(time_t epoch, tm &out);

// Day key YYYYMMDD of `epoch`, in the device's local time zone or in UTC.
// False, leaving `day_key` unchanged, unless the time is trusted
// (after Config::TIME_VALID_EPOCH) and the date is a real date in 2020 or later.
bool localDayKey(time_t epoch, uint32_t &day_key);
bool utcDayKey(time_t epoch, uint32_t &day_key);

// "YYYY-MM-DD" from a day key. Writes nothing if `out` is null or `len` is 0.
void formatDayKey(uint32_t day_key, char *out, size_t len);
// Local "HH:MM:SS". Empty string if the conversion fails.
void formatLocalTime(time_t epoch, char *out, size_t len);

} // namespace LocalDate
