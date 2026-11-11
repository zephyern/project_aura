// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "modules/ChartsHistory.h"

// Which series the multi-sensor chart shows (docs/CHARTS_HISTORY.md). Series
// are grouped by unit; the chart has two Y axes, so at most two units can be
// selected at once. Hiding a selected series keeps it selected (and its data
// loaded) and keeps its unit on its axis; only removing it frees the axis.
namespace MultiChartSelection {

constexpr size_t kMaxUnits = 2;
// One series per sensor metric. Layer E (outdoor temperature) adds one more
// series here with the unit "C", so it shares the indoor temperature's axis.
constexpr size_t kMaxSeries = ChartsHistory::kMetricCount;

struct Series {
    int id;           // ChartsHistory::Metric for a sensor
    const char *unit; // as in WebChartsUtils' table; equal strings share an axis
};

enum class Axis : uint8_t { Left, Right };

enum class Toggle : uint8_t {
    Added,
    Removed,
    ThirdUnit,     // not added: two other units are already selected
    UnknownSeries, // index outside the series list
};

// Every sensor series in the dashboard's order (WebChartsUtils), with the
// optional gas in `optional_gas_unit` (it depends on the fitted sensor, e.g.
// ppb for ozone). False when `capacity` is too small or the unit is missing.
bool sensorSeries(const char *optional_gas_unit, Series *out, size_t capacity, size_t &count);

class Selection {
public:
    // False (and nothing selectable) for more than kMaxSeries series, a null
    // list, or a series without a unit.
    bool init(const Series *series, size_t count);

    size_t seriesCount() const { return count_; }
    const Series &series(size_t index) const { return series_[index]; }

    // Units in the order they first appear in the series list, and the group
    // of each series: the selection screen lists each group under its unit.
    size_t groupCount() const { return group_count_; }
    const char *groupUnit(size_t group) const { return group_unit_[group]; }
    size_t groupOf(size_t index) const { return group_of_[index]; }

    // Adds an unselected series, removes a selected one (shown or hidden).
    Toggle toggle(size_t index);
    // Hides or shows a selected series; false for an unselected or unknown one.
    bool setHidden(size_t index, bool hidden);

    bool selected(size_t index) const { return index < count_ && selected_[index]; }
    bool visible(size_t index) const { return selected(index) && !hidden_[index]; }
    // The axis of a selected series; false otherwise.
    bool axisOf(size_t index, Axis &axis) const;
    // The unit on an axis, or nullptr when the axis is free.
    const char *axisUnit(Axis axis) const;

private:
    static constexpr uint8_t kNoGroup = UINT8_MAX;

    bool groupSelected(uint8_t group) const;

    Series series_[kMaxSeries] = {};
    size_t count_ = 0;
    const char *group_unit_[kMaxSeries] = {};
    uint8_t group_of_[kMaxSeries] = {};
    size_t group_count_ = 0;
    bool selected_[kMaxSeries] = {};
    bool hidden_[kMaxSeries] = {};
    // A unit keeps its axis while any of its series stays selected, so the
    // other unit never jumps sides.
    uint8_t axis_group_[kMaxUnits] = {kNoGroup, kNoGroup};
};

} // namespace MultiChartSelection
