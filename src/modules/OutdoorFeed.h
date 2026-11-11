// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "config/AppData.h"
#include "modules/ChartsHistory.h"
#include "modules/WeatherManager.h"

// Carries the latest weather result into SensorData (docs/WEATHER.md). Valid
// only while the latest check succeeded, so a failed check is a gap at once.
namespace OutdoorFeed {

inline void apply(const WeatherManager::Snapshot &snapshot, SensorData &data) {
    const bool ok = snapshot.status == WeatherApi::Status::Ok;
    data.outdoor_temp_valid = ok;
    data.outdoor_temp = ok ? snapshot.temp_c : 0.0f;
    data.outdoor_temp_source = ok ? ChartsHistory::OUTDOOR_SOURCE_API : ChartsHistory::OUTDOOR_SOURCE_NONE;
}

// The chart-history fresh bit: set once per new successful reading.
inline uint16_t freshBit(const WeatherManager::Snapshot &snapshot, uint32_t &last_seq) {
    const bool fresh = snapshot.seq != last_seq && snapshot.status == WeatherApi::Status::Ok;
    last_seq = snapshot.seq;
    return fresh ? ChartsHistory::metricBit(ChartsHistory::METRIC_OUTDOOR_TEMP) : 0;
}

}  // namespace OutdoorFeed
