// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modules/StorageHealth.h"

#include <string.h>

namespace StorageHealth {
namespace {

constexpr uint32_t kHoursPerDay = 24;
constexpr const char *kAutoDeleteName = "auto_delete";
constexpr const char *kStopRecordingName = "stop_recording";

} // namespace

const char *fullActionName(FullAction action) {
    return action == FullAction::StopRecording ? kStopRecordingName : kAutoDeleteName;
}

bool parseFullAction(const char *text, FullAction &out) {
    if (!text) return false;
    if (strcmp(text, kAutoDeleteName) == 0) {
        out = FullAction::AutoDelete;
        return true;
    }
    if (strcmp(text, kStopRecordingName) == 0) {
        out = FullAction::StopRecording;
        return true;
    }
    return false;
}

uint32_t daysLeft(uint64_t free_bytes, uint32_t bytes_per_day) {
    if (bytes_per_day == 0) return UINT32_MAX;
    const uint64_t days = free_bytes / bytes_per_day;
    return days > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(days);
}

uint32_t bytesPerDay(const RateSample &sample) {
    if (!sample.quarter_file_bytes || sample.days == 0) return 0;
    uint64_t total = 0;
    for (size_t i = 0; i < sample.days; ++i) total += sample.quarter_file_bytes[i];
    uint64_t per_day = total / sample.days;
    per_day += static_cast<uint64_t>(sample.hourly_record_bytes) * kHoursPerDay;
    per_day += sample.daily_record_bytes;
    return per_day > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(per_day);
}

Status evaluate(const Inputs &in) {
    Status out;
    if (!in.log_enabled) return out;
    if (!in.card_ready) {
        out.kind = Kind::NoCard;
        return out;
    }
    const bool no_space = in.write_error && in.failure_kind == DailyStorageFailureKind::NoSpace;
    const bool full = no_space || (in.bytes_per_day != 0 && in.free_bytes < in.bytes_per_day);
    if (full && (in.full_action == FullAction::StopRecording || in.nothing_left_to_delete)) {
        out.kind = Kind::RecordingStopped;
        return out;
    }
    if (in.write_error && !no_space) {
        out.kind = Kind::WriteError;
        out.failure_kind = in.failure_kind;
        return out;
    }
    if (in.clock_behind) {
        out.kind = Kind::ClockBehind;
        return out;
    }
    if (full) {
        out.kind = Kind::Full;
        return out;
    }
    const uint32_t days = daysLeft(in.free_bytes, in.bytes_per_day);
    if (days < kLowSpaceDays) {
        out.kind = Kind::LowSpace;
        out.days_left = days;
        return out;
    }
    out.kind = Kind::Ok;
    return out;
}

} // namespace StorageHealth
