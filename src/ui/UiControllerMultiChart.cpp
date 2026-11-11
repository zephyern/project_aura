// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

// The multi-sensor chart (charts.multi_enabled, docs/CHARTS_HISTORY.md):
// holding a sensor chart opens a full-screen chart of several sensors at
// once, one Y axis per unit (left and right). LINES opens the list of every
// sensor grouped by unit; tapping a legend chip hides or shows that line.

#include "ui/UiController.h"

#include <math.h>
#include <stdio.h>

#include "config/ChartsSettings.h"
#include "core/Logger.h"
#include "drivers/DfrOptionalGasSensor.h"
#include "modules/ChartsHistory.h"
#include "ui/LvglRelease.h"
#include "ui/UiControllerGraphsShared.h"
#include "ui/UiLocalization.h"
#include "ui/UiOptionalGasProfile.h"
#include "ui/UiText.h"
#include "ui/ui.h"

namespace {

constexpr const char *kTag = "MultiChart";

using Selection = MultiChartSelection::Selection;
using SelAxis = MultiChartSelection::Axis;

constexpr lv_opa_t kHiddenChipOpa = LV_OPA_40;
constexpr lv_coord_t kDotSize = 12;
constexpr lv_coord_t kChipPad = 6;
constexpr lv_coord_t kChipGap = 8;
constexpr lv_coord_t kChipRadius = 8;
constexpr lv_coord_t kRowHeight = 40;
constexpr lv_coord_t kRowPad = 10;
constexpr lv_coord_t kRowRadius = 10;
constexpr lv_coord_t kAxisLabelWidth = 56;
constexpr lv_coord_t kAxisLabelGap = 4;
constexpr size_t kAxisLabelCapacity = 16;

lv_chart_axis_t chartAxis(SelAxis axis) {
    return axis == SelAxis::Left ? LV_CHART_AXIS_PRIMARY_Y : LV_CHART_AXIS_SECONDARY_Y;
}

lv_obj_t *unitLabel(SelAxis axis) {
    return axis == SelAxis::Left ? objects.label_multi_chart_unit_left : objects.label_multi_chart_unit_right;
}

void setHidden(lv_obj_t *obj, bool hidden) {
    if (obj) lv_obj_set_hidden(obj, hidden);
}

void cleanContainer(lv_obj_t *container) {
    if (container && lv_obj_is_valid(container)) lv_obj_clean(container);
}

} // namespace

static_assert(ChartBand::kNoValue == LV_CHART_POINT_NONE, "line gaps use the chart's gap marker");

// How each sensor is drawn, indexed by ChartsHistory::Metric: its colour and
// the same display rules as its own chart.
struct UiController::MultiSeriesStyle {
    uint32_t colour;
    GraphValueTransform transform;
    bool require_non_negative;
};

const UiController::MultiSeriesStyle UiController::kMultiSeriesStyles[ChartsHistory::kMetricCount] = {
    {0x4FC3F7, GraphValueTransform::None, false},        // CO2
    {0xFF8A65, GraphValueTransform::Temperature, false}, // temperature
    {0x4DB6AC, GraphValueTransform::None, false},        // humidity
    {0xBA68C8, GraphValueTransform::Pressure, false},    // pressure
    {0xE57373, GraphValueTransform::None, true},         // CO
    {0xAED581, GraphValueTransform::None, false},        // VOC
    {0xFFD54F, GraphValueTransform::None, false},        // NOx
    {0xF06292, GraphValueTransform::None, false},        // HCHO
    {0x90A4AE, GraphValueTransform::None, true},         // PM0.5
    {0x7986CB, GraphValueTransform::None, true},         // PM1
    {0xFFB74D, GraphValueTransform::None, true},         // PM2.5
    {0xA1887F, GraphValueTransform::None, true},         // PM4
    {0x81C784, GraphValueTransform::None, true},         // PM10
    {0x64B5F6, GraphValueTransform::None, true},         // optional gas
    {0xFFF176, GraphValueTransform::Temperature, false}, // outdoor temperature
};

const UiController::MultiRange UiController::kMultiRanges[] = {
    {&objects_t::btn_multi_chart_range_1h, TEMP_GRAPH_RANGE_1H},
    {&objects_t::btn_multi_chart_range_3h, TEMP_GRAPH_RANGE_3H},
    {&objects_t::btn_multi_chart_range_24h, TEMP_GRAPH_RANGE_24H},
};
const size_t UiController::kMultiRangeCount = sizeof(kMultiRanges) / sizeof(kMultiRanges[0]);

lv_obj_t *objects_t::*const UiController::kInfoCharts[] = {
    &objects_t::chart_temp_info,     &objects_t::chart_co2_info,          &objects_t::chart_voc_info,
    &objects_t::chart_optional_gas_info, &objects_t::chart_nox_info,      &objects_t::chart_hcho_info,
    &objects_t::chart_rh_info,       &objects_t::chart_pressure_info,     &objects_t::chart_pm1_10_info,
    &objects_t::chart_pm25_4_graph,  &objects_t::chart_pm05_info,         &objects_t::chart_co_info,
};
const size_t UiController::kInfoChartCount = sizeof(kInfoCharts) / sizeof(kInfoCharts[0]);

bool UiController::multi_chart_enabled() const {
    return storage.config().charts.multi_enabled;
}

bool UiController::info_sensor_metric(InfoSensor sensor, ChartsHistory::Metric &metric) {
    switch (sensor) {
        case INFO_TEMP: metric = ChartsHistory::METRIC_TEMPERATURE; return true;
        case INFO_VOC: metric = ChartsHistory::METRIC_VOC; return true;
        case INFO_NOX: metric = ChartsHistory::METRIC_NOX; return true;
        case INFO_OPTIONAL_GAS: metric = ChartsHistory::METRIC_OPTIONAL_GAS; return true;
        case INFO_HCHO: metric = ChartsHistory::METRIC_HCHO; return true;
        case INFO_CO2: metric = ChartsHistory::METRIC_CO2; return true;
        case INFO_RH: metric = ChartsHistory::METRIC_HUMIDITY; return true;
        case INFO_PM05: metric = ChartsHistory::METRIC_PM05; return true;
        case INFO_PM25: metric = ChartsHistory::METRIC_PM25; return true;
        case INFO_PM4: metric = ChartsHistory::METRIC_PM4; return true;
        case INFO_PM10: metric = ChartsHistory::METRIC_PM10; return true;
        case INFO_PM1: metric = ChartsHistory::METRIC_PM1; return true;
        case INFO_CO: metric = ChartsHistory::METRIC_CO; return true;
        case INFO_PRESSURE_3H:
        case INFO_PRESSURE_24H: metric = ChartsHistory::METRIC_PRESSURE; return true;
        // Derived views (AQI, absolute humidity, mixing ratio, dew point) have
        // no history series of their own.
        case INFO_NONE:
        case INFO_AQI:
        case INFO_AH:
        case INFO_MR:
        case INFO_DP: return false;
    }
    return false;
}

const char *UiController::multi_series_name(int metric) const {
    switch (metric) {
        case ChartsHistory::METRIC_CO2: return "CO2";
        case ChartsHistory::METRIC_TEMPERATURE: return UiText::SensorInfoTitleTemperature();
        case ChartsHistory::METRIC_HUMIDITY: return UiText::SensorInfoTitleRh();
        case ChartsHistory::METRIC_PRESSURE: return UiText::LabelPressureTitle();
        case ChartsHistory::METRIC_CO: return "CO";
        case ChartsHistory::METRIC_VOC: return "VOC";
        case ChartsHistory::METRIC_NOX: return "NOx";
        case ChartsHistory::METRIC_HCHO: return UiText::SensorInfoTitleFormaldehyde();
        case ChartsHistory::METRIC_PM05: return "PM0.5";
        case ChartsHistory::METRIC_PM1: return "PM1";
        case ChartsHistory::METRIC_PM25: return "PM2.5";
        case ChartsHistory::METRIC_PM4: return "PM4";
        case ChartsHistory::METRIC_PM10: return "PM10";
        case ChartsHistory::METRIC_OPTIONAL_GAS: {
            using OptionalGasType = DfrOptionalGasSensor::OptionalGasType;
            return UiOptionalGasProfile::forType(static_cast<OptionalGasType>(currentData.optional_gas_type)).label;
        }
    }
    LOGE(kTag, "no name for chart series %d", metric);
    return "";
}

const char *UiController::multi_series_unit(int metric) const {
    switch (metric) {
        case ChartsHistory::METRIC_CO2:
        case ChartsHistory::METRIC_CO: return "ppm";
        case ChartsHistory::METRIC_TEMPERATURE:
        case ChartsHistory::METRIC_OUTDOOR_TEMP: return temp_units_c ? UiText::UnitC() : UiText::UnitF();
        case ChartsHistory::METRIC_HUMIDITY: return "%";
        case ChartsHistory::METRIC_PRESSURE: return pressure_display_unit();
        case ChartsHistory::METRIC_VOC:
        case ChartsHistory::METRIC_NOX: return UiText::UnitIndex();
        case ChartsHistory::METRIC_HCHO: return "ppb";
        case ChartsHistory::METRIC_PM05: return "#/cm3";
        case ChartsHistory::METRIC_PM1:
        case ChartsHistory::METRIC_PM25:
        case ChartsHistory::METRIC_PM4:
        case ChartsHistory::METRIC_PM10: return "ug/m3";
        case ChartsHistory::METRIC_OPTIONAL_GAS: {
            using OptionalGasType = DfrOptionalGasSensor::OptionalGasType;
            return UiOptionalGasProfile::forType(static_cast<OptionalGasType>(currentData.optional_gas_type)).unit;
        }
    }
    LOGE(kTag, "no unit for chart series %d", metric);
    return "";
}

bool UiController::ensure_multi_selection() {
    using OptionalGasType = DfrOptionalGasSensor::OptionalGasType;
    const char *gas_unit =
        DfrOptionalGasSensor::unitForType(static_cast<OptionalGasType>(currentData.optional_gas_type));
    if (multi_selection_ready_ && gas_unit == multi_gas_unit_) return true;
    // A different optional gas can change its unit group, so the selection
    // starts again.
    MultiChartSelection::Series series[MultiChartSelection::kMaxSeries];
    size_t count = 0;
    if (!MultiChartSelection::sensorSeries(gas_unit, series, MultiChartSelection::kMaxSeries, count) ||
        !multi_selection_.init(series, count)) {
        LOGE(kTag, "cannot build the sensor series list");
        multi_selection_ready_ = false;
        return false;
    }
    multi_selection_ready_ = true;
    multi_gas_unit_ = gas_unit;
    return true;
}

void UiController::on_info_chart_long_press(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED || !multi_chart_enabled()) return;
    if (!ensure_multi_selection()) return;
    ChartsHistory::Metric metric = ChartsHistory::METRIC_COUNT;
    if (info_sensor_metric(info_sensor, metric)) {
        open_multi_chart(metric);
        return;
    }
    // A derived view: open with the lines already chosen.
    multi_third_unit_ = false;
    pending_screen_id = SCREEN_ID_PAGE_MULTI_CHART;
}

void UiController::open_multi_chart(ChartsHistory::Metric metric) {
    multi_third_unit_ = false;
    for (size_t i = 0; i < multi_selection_.seriesCount(); ++i) {
        if (multi_selection_.series(i).id != metric) continue;
        if (!multi_selection_.selected(i)) {
            // Two other units already on: the chart opens on them and the
            // lines list explains why this one was not added.
            if (multi_selection_.toggle(i) == MultiChartSelection::Toggle::ThirdUnit) {
                multi_third_unit_ = true;
                multi_lines_open_ = true;
            }
        } else {
            multi_selection_.setHidden(i, false);
        }
        break;
    }
    pending_screen_id = SCREEN_ID_PAGE_MULTI_CHART;
}

void UiController::enter_multi_chart() {
    if (!ensure_multi_selection()) {
        pending_screen_id = SCREEN_ID_PAGE_SENSORS_INFO;
        return;
    }
    multi_drawn_ = MultiChartDrawn{};
    multi_dirty_ = true;
    update_multi_chart_texts();
    rebuild_multi_chart_lists();
    sync_multi_chart_controls();
    data_dirty = true;
}

void UiController::release_multi_chart_runtime_objects() {
    for (size_t a = 0; a < kMultiAxisCount; ++a) {
        release_lvgl_obj_array(multi_axis_labels_[a], kMultiAxisLabelCount);
    }
    release_lvgl_obj_array(multi_time_labels_, sizeof(multi_time_labels_) / sizeof(multi_time_labels_[0]));
    cleanContainer(objects.container_multi_chart_legend);
    cleanContainer(objects.container_multi_chart_lines);
    multi_lines_open_ = false;
    multi_third_unit_ = false;
    multi_drawn_ = MultiChartDrawn{};
    multi_samples_.clear();
    multi_samples_.shrink_to_fit();
    multi_values_.clear();
    multi_values_.shrink_to_fit();
}

void UiController::on_multi_chart_back(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    restore_sensor_info_selection();
    pending_screen_id = SCREEN_ID_PAGE_SENSORS_INFO;
}

void UiController::on_multi_chart_range(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    multi_range_ = static_cast<TempGraphRange>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    multi_dirty_ = true;
    sync_multi_chart_controls();
    update_multi_chart();
}

void UiController::on_multi_chart_lines(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    multi_lines_open_ = !multi_lines_open_;
    multi_third_unit_ = false;
    multi_dirty_ = true;
    rebuild_multi_chart_lists();
    sync_multi_chart_controls();
    update_multi_chart();
}

void UiController::on_multi_chart_row(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    switch (multi_selection_.toggle(index)) {
        case MultiChartSelection::Toggle::Added:
        case MultiChartSelection::Toggle::Removed:
            multi_third_unit_ = false;
            multi_dirty_ = true;
            break;
        case MultiChartSelection::Toggle::ThirdUnit:
            multi_third_unit_ = true;
            break;
        case MultiChartSelection::Toggle::UnknownSeries:
            LOGE(kTag, "unknown series row %u", static_cast<unsigned>(index));
            return;
    }
    rebuild_multi_chart_lists();
    sync_multi_chart_controls();
}

void UiController::on_multi_chart_chip(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (!multi_selection_.setHidden(index, multi_selection_.visible(index))) {
        LOGE(kTag, "legend chip for unselected series %u", static_cast<unsigned>(index));
        return;
    }
    multi_dirty_ = true;
    rebuild_multi_chart_lists();
    sync_multi_chart_controls();
    update_multi_chart();
}

lv_obj_t *UiController::add_multi_series_chip(lv_obj_t *parent, size_t index, lv_event_cb_t cb) {
    const int metric = multi_selection_.series(index).id;
    const lv_font_t *font = UiLocalization::bodyFont14(ui_language);
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(chip, kChipPad, LV_PART_MAIN);
    lv_obj_set_style_pad_column(chip, kChipGap, LV_PART_MAIN);
    lv_obj_set_style_radius(chip, kChipRadius, LV_PART_MAIN);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(chip, true);
    lv_obj_set_scrollable(chip, false);
    lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));

    lv_obj_t *dot = lv_obj_create(chip);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, kDotSize, kDotSize);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(kMultiSeriesStyles[metric].colour), LV_PART_MAIN);
    lv_obj_set_clickable(dot, false);

    lv_obj_t *name = lv_label_create(chip);
    lv_obj_set_style_text_font(name, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(name, active_text_color(), LV_PART_MAIN);
    lv_label_set_text(name, multi_series_name(metric));
    return chip;
}

void UiController::rebuild_multi_chart_lists() {
    lv_obj_t *legend = objects.container_multi_chart_legend;
    lv_obj_t *lines = objects.container_multi_chart_lines;
    if (!legend || !lines) return;
    cleanContainer(legend);
    cleanContainer(lines);

    if (!multi_lines_open_) {
        for (size_t i = 0; i < multi_selection_.seriesCount(); ++i) {
            if (!multi_selection_.selected(i)) continue;
            lv_obj_t *chip = add_multi_series_chip(legend, i, &UiController::on_multi_chart_chip_event_cb);
            if (!multi_selection_.visible(i)) lv_obj_set_style_opa(chip, kHiddenChipOpa, LV_PART_MAIN);
        }
        return;
    }

    // Every sensor, grouped under its unit, the unit beside each name.
    const lv_font_t *font = UiLocalization::bodyFont18(ui_language);
    const lv_color_t text = active_text_color();
    for (size_t g = 0; g < multi_selection_.groupCount(); ++g) {
        bool header_done = false;
        for (size_t i = 0; i < multi_selection_.seriesCount(); ++i) {
            if (multi_selection_.groupOf(i) != g) continue;
            const int metric = multi_selection_.series(i).id;
            if (!header_done) {
                lv_obj_t *header = lv_label_create(lines);
                lv_obj_set_style_text_font(header, font, LV_PART_MAIN);
                lv_obj_set_style_text_color(header, color_card_border(), LV_PART_MAIN);
                lv_label_set_text(header, multi_series_unit(metric));
                header_done = true;
            }
            lv_obj_t *row = add_multi_series_chip(lines, i, &UiController::on_multi_chart_row_event_cb);
            lv_obj_set_size(row, lv_pct(100), kRowHeight);
            lv_obj_set_style_pad_hor(row, kRowPad, LV_PART_MAIN);
            lv_obj_set_style_radius(row, kRowRadius, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(row, color_card_border(), LV_PART_MAIN);
            apply_toggle_style(row);
            lv_obj_set_state(row, LV_STATE_CHECKED, multi_selection_.selected(i));
            lv_obj_t *name = lv_obj_get_child(row, 1);
            if (name) {
                lv_obj_set_style_text_font(name, font, LV_PART_MAIN);
                lv_obj_set_flex_grow(name, 1);
            }
            lv_obj_t *unit = lv_label_create(row);
            lv_obj_set_style_text_font(unit, font, LV_PART_MAIN);
            lv_obj_set_style_text_color(unit, text, LV_PART_MAIN);
            lv_label_set_text(unit, multi_series_unit(metric));
        }
    }
}

void UiController::sync_multi_chart_controls() {
    for (size_t i = 0; i < kMultiRangeCount; ++i) {
        lv_obj_t *btn = objects.*kMultiRanges[i].button;
        if (btn) lv_obj_set_state(btn, LV_STATE_CHECKED, kMultiRanges[i].range == multi_range_);
    }
    if (objects.btn_multi_chart_lines) lv_obj_set_state(objects.btn_multi_chart_lines, LV_STATE_CHECKED, multi_lines_open_);

    bool any_selected = false;
    for (size_t i = 0; i < multi_selection_.seriesCount(); ++i) any_selected |= multi_selection_.selected(i);

    setHidden(objects.container_multi_chart_lines, !multi_lines_open_);
    setHidden(objects.container_multi_chart_legend, multi_lines_open_);
    setHidden(objects.chart_multi_chart, multi_lines_open_);
    setHidden(objects.label_multi_chart_unit_left, multi_lines_open_);
    setHidden(objects.label_multi_chart_unit_right, multi_lines_open_);
    if (multi_lines_open_) {
        for (size_t a = 0; a < kMultiAxisCount; ++a) {
            release_lvgl_obj_array(multi_axis_labels_[a], kMultiAxisLabelCount);
        }
        release_lvgl_obj_array(multi_time_labels_, sizeof(multi_time_labels_) / sizeof(multi_time_labels_[0]));
    }

    const char *message = nullptr;
    if (multi_lines_open_ && multi_third_unit_) {
        message = UiText::MsgMultiChartThirdUnit();
    } else if (!multi_lines_open_ && !any_selected) {
        message = UiText::MsgMultiChartEmpty();
    }
    if (objects.label_multi_chart_message) {
        safe_label_set_text(objects.label_multi_chart_message, message ? message : "");
        lv_obj_set_style_text_font(objects.label_multi_chart_message, UiLocalization::bodyFont14(ui_language),
                                   LV_PART_MAIN);
        setHidden(objects.label_multi_chart_message, message == nullptr);
        if (message) lv_obj_move_foreground(objects.label_multi_chart_message);
    }
}

void UiController::update_multi_chart_texts() {
    safe_label_set_text(objects.label_multi_chart_title, UiText::LabelMultiChartTitle());
    safe_label_set_text(objects.label_btn_multi_chart_lines, UiText::LabelMultiChartLines());
    safe_label_set_text(objects.label_btn_multi_chart_back, UiText::LabelDiagBack());
    if (current_screen_id == SCREEN_ID_PAGE_MULTI_CHART && multi_selection_ready_) {
        // Series names and the messages follow the language too.
        rebuild_multi_chart_lists();
        sync_multi_chart_controls();
        multi_dirty_ = true;
    }
}

void UiController::update_multi_chart() {
    if (current_screen_id != SCREEN_ID_PAGE_MULTI_CHART) return;
    if (!multi_chart_enabled()) {
        // Turned off from the dashboard while showing.
        restore_sensor_info_selection();
        pending_screen_id = SCREEN_ID_PAGE_SENSORS_INFO;
        return;
    }
    if (multi_lines_open_ || !multi_selection_ready_) return;
    MultiChartDrawn now;
    now.valid = true;
    now.count = chartsHistory.count();
    ChartsHistory::Entry latest;
    if (now.count > 0 && chartsHistory.entryFromOldest(static_cast<uint16_t>(now.count - 1), latest)) {
        now.latest_epoch = latest.start_epoch;
    }
    now.theme = active_graph_theme_signature();
    now.units_c = temp_units_c;
    if (!multi_dirty_ && multi_drawn_.valid && multi_drawn_.count == now.count &&
        multi_drawn_.latest_epoch == now.latest_epoch && multi_drawn_.theme == now.theme &&
        multi_drawn_.units_c == now.units_c) {
        return;
    }
    draw_multi_chart();
    multi_drawn_ = now;
    multi_dirty_ = false;
}

UiController::MultiAxisFit UiController::fit_multi_axis(SelAxis axis, uint16_t points) {
    MultiAxisFit fit;
    bool any = false;
    float low = 0.0f;
    float high = 0.0f;
    for (size_t i = 0; i < multi_selection_.seriesCount(); ++i) {
        SelAxis on = SelAxis::Left;
        if (!multi_selection_.visible(i) || !multi_selection_.axisOf(i, on) || on != axis) continue;
        const int metric = multi_selection_.series(i).id;
        const MultiSeriesStyle &style = kMultiSeriesStyles[metric];
        read_recent_samples(static_cast<ChartsHistory::Metric>(metric), points, multi_samples_);
        multi_values_.resize(points);
        const ChartPlot::Stats stats = ChartPlot::plotLine(
            multi_samples_.data(), points, graph_plot_options(1.0f, style.require_non_negative, style.transform),
            multi_values_.data());
        if (!stats.has_values) continue;
        low = any ? fminf(low, stats.scale_min) : stats.scale_min;
        high = any ? fmaxf(high, stats.scale_max) : stats.scale_max;
        any = true;
    }
    if (!any) return fit;
    if (!MultiChartAxis::fit(low, high, fit.axis)) {
        LOGE(kTag, "no axis for %f..%f", static_cast<double>(low), static_cast<double>(high));
        return fit;
    }
    fit.valid = true;
    return fit;
}

void UiController::plot_multi_axis(SelAxis axis, const MultiAxisFit &fit, uint16_t points) {
    lv_obj_t *chart = objects.chart_multi_chart;
    if (!fit.valid) return;
    lv_chart_set_range(chart, chartAxis(axis), static_cast<int32_t>(lroundf(fit.axis.min * fit.axis.scale)),
                       static_cast<int32_t>(lroundf(fit.axis.max() * fit.axis.scale)));
    for (size_t i = 0; i < multi_selection_.seriesCount(); ++i) {
        SelAxis on = SelAxis::Left;
        if (!multi_selection_.visible(i) || !multi_selection_.axisOf(i, on) || on != axis) continue;
        const int metric = multi_selection_.series(i).id;
        const MultiSeriesStyle &style = kMultiSeriesStyles[metric];
        lv_chart_series_t *series = lv_chart_add_series(chart, lv_color_hex(style.colour), chartAxis(axis));
        if (!series) {
            LOGE(kTag, "no memory for chart series %d", metric);
            return;
        }
        read_recent_samples(static_cast<ChartsHistory::Metric>(metric), points, multi_samples_);
        multi_values_.resize(points);
        ChartPlot::plotLine(multi_samples_.data(), points,
                            graph_plot_options(fit.axis.scale, style.require_non_negative, style.transform),
                            multi_values_.data());
        for (uint16_t p = 0; p < points; ++p) {
            lv_chart_set_value_by_id(chart, series, p, multi_values_[p]);
        }
    }
}

void UiController::place_multi_axis_labels(SelAxis axis, const MultiAxisFit &fit) {
    lv_obj_t *chart = objects.chart_multi_chart;
    lv_obj_t *card = objects.card_multi_chart;
    lv_obj_t **labels = multi_axis_labels_[static_cast<size_t>(axis)];
    const char *unit = multi_selection_.axisUnit(axis);
    lv_obj_t *unit_label = unitLabel(axis);
    if (unit_label) {
        // The axis unit as shown (°F, inHg, ...), from the axis' first series.
        const char *shown = "";
        for (size_t i = 0; unit && i < multi_selection_.seriesCount(); ++i) {
            SelAxis on = SelAxis::Left;
            if (multi_selection_.axisOf(i, on) && on == axis) {
                shown = multi_series_unit(multi_selection_.series(i).id);
                break;
            }
        }
        safe_label_set_text(unit_label, shown);
    }
    if (!fit.valid) {
        release_lvgl_obj_array(labels, kMultiAxisLabelCount);
        return;
    }
    lv_obj_update_layout(chart);
    const lv_coord_t chart_x = lv_obj_get_x(chart);
    const lv_coord_t chart_y = lv_obj_get_y(chart);
    const lv_coord_t chart_w = lv_obj_get_width(chart);
    const lv_coord_t chart_h = lv_obj_get_height(chart);
    const bool left = axis == SelAxis::Left;
    char buf[kAxisLabelCapacity];
    for (uint8_t i = 0; i < kMultiAxisLabelCount; ++i) {
        lv_obj_t *&label = labels[i];
        if (!label || !lv_obj_is_valid(label)) {
            label = lv_label_create(card);
            lv_obj_set_width(label, kAxisLabelWidth);
            lv_obj_set_style_text_font(label, &ui_font_jet_reg_14, LV_PART_MAIN);
            lv_obj_set_style_text_align(label, left ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        }
        lv_obj_set_style_text_color(label, active_text_color(), LV_PART_MAIN);
        const float value = fit.axis.min + fit.axis.step * static_cast<float>(i);
        snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(fit.axis.decimals), static_cast<double>(value));
        lv_label_set_text(label, buf);
        lv_obj_update_layout(label);
        const lv_coord_t y = chart_y + (chart_h * (MultiChartAxis::kDivisions - i)) / MultiChartAxis::kDivisions -
                             lv_obj_get_height(label) / 2;
        const lv_coord_t x = left ? chart_x - kAxisLabelGap - kAxisLabelWidth : chart_x + chart_w + kAxisLabelGap;
        lv_obj_set_pos(label, x, y);
    }
}

void UiController::draw_multi_chart() {
    lv_obj_t *chart = objects.chart_multi_chart;
    if (!chart || !objects.card_multi_chart) return;
    const uint16_t points = graph_points_for_range(multi_range_);
    apply_standard_info_chart_theme(chart, MultiChartAxis::kDivisions + 1,
                                    graph_vertical_divisions_for_range(multi_range_));
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, nullptr); s;
         s = lv_chart_get_series_next(chart, nullptr)) {
        lv_chart_remove_series(chart, s);
    }
    lv_chart_set_point_count(chart, points);
    for (const SelAxis axis : {SelAxis::Left, SelAxis::Right}) {
        const MultiAxisFit fit = fit_multi_axis(axis, points);
        plot_multi_axis(axis, fit, points);
        place_multi_axis_labels(axis, fit);
    }
    chart_axis_ = ChartAxis{};
    update_graph_time_labels(objects.card_multi_chart, chart, multi_time_labels_,
                             sizeof(multi_time_labels_) / sizeof(multi_time_labels_[0]), points);
    lv_chart_refresh(chart);
}

void UiController::on_info_chart_long_press_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_info_chart_long_press(e);
}
void UiController::on_multi_chart_back_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_multi_chart_back(e);
}
void UiController::on_multi_chart_range_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_multi_chart_range(e);
}
void UiController::on_multi_chart_lines_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_multi_chart_lines(e);
}
void UiController::on_multi_chart_row_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_multi_chart_row(e);
}
void UiController::on_multi_chart_chip_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_multi_chart_chip(e);
}
