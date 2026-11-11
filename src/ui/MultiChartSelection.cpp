// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/MultiChartSelection.h"

#include <string.h>

#include "web/WebChartsUtils.h"

namespace MultiChartSelection {

bool sensorSeries(const char *optional_gas_unit, Series *out, size_t capacity, size_t &count) {
    count = 0;
    const size_t total = WebChartsUtils::chartMetricCount();
    if (!optional_gas_unit || !out || capacity < total) return false;
    for (size_t i = 0; i < total; ++i) {
        const WebChartsUtils::ChartMetricSpec *spec = nullptr;
        if (!WebChartsUtils::chartMetricAt(i, spec)) return false;
        const bool gas = spec->metric == ChartsHistory::METRIC_OPTIONAL_GAS;
        out[i] = Series{spec->metric, gas ? optional_gas_unit : spec->unit};
    }
    count = total;
    return true;
}

bool Selection::init(const Series *series, size_t count) {
    *this = Selection{};
    if (!series || count > kMaxSeries) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!series[i].unit) {
            *this = Selection{};
            return false;
        }
        series_[i] = series[i];
        size_t group = 0;
        while (group < group_count_ && strcmp(group_unit_[group], series[i].unit) != 0) ++group;
        if (group == group_count_) group_unit_[group_count_++] = series[i].unit;
        group_of_[i] = static_cast<uint8_t>(group);
    }
    count_ = count;
    return true;
}

bool Selection::groupSelected(uint8_t group) const {
    for (size_t i = 0; i < count_; ++i) {
        if (selected_[i] && group_of_[i] == group) return true;
    }
    return false;
}

Toggle Selection::toggle(size_t index) {
    if (index >= count_) return Toggle::UnknownSeries;
    const uint8_t group = group_of_[index];
    if (selected_[index]) {
        selected_[index] = false;
        hidden_[index] = false;
        if (!groupSelected(group)) {
            for (uint8_t &axis_group : axis_group_) {
                if (axis_group == group) axis_group = kNoGroup;
            }
        }
        return Toggle::Removed;
    }
    bool has_axis = false;
    for (const uint8_t axis_group : axis_group_) has_axis = has_axis || axis_group == group;
    if (!has_axis) {
        uint8_t *free_axis = nullptr;
        for (uint8_t &axis_group : axis_group_) {
            if (axis_group == kNoGroup && !free_axis) free_axis = &axis_group;
        }
        if (!free_axis) return Toggle::ThirdUnit;
        *free_axis = group;
    }
    selected_[index] = true;
    return Toggle::Added;
}

bool Selection::setHidden(size_t index, bool hidden) {
    if (!selected(index)) return false;
    hidden_[index] = hidden;
    return true;
}

const char *Selection::axisUnit(Axis axis) const {
    const uint8_t group = axis_group_[static_cast<size_t>(axis)];
    return group == kNoGroup ? nullptr : group_unit_[group];
}

bool Selection::axisOf(size_t index, Axis &axis) const {
    if (!selected(index)) return false;
    for (size_t a = 0; a < kMaxUnits; ++a) {
        if (axis_group_[a] == group_of_[index]) {
            axis = static_cast<Axis>(a);
            return true;
        }
    }
    return false;
}

} // namespace MultiChartSelection
