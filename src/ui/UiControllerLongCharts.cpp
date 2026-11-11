// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

// The long chart ranges (docs/CHARTS_HISTORY.md): the Month button of every
// range group, the ‹ › period header and the charts read from the long-term
// history files.

#include "ui/UiController.h"

#include <time.h>

#include "core/Logger.h"
#include "modules/ChartsHistory.h"
#include "modules/HourlySummary.h"
#include "modules/LongHistory.h"
#include "ui/UiText.h"
#include "ui/ui.h"

namespace {

constexpr const char *kTag = "Charts";
// Month view: the line has one point per three hours, the band one per hour.
constexpr uint32_t kMonthLineSlotSeconds = 3U * HourlySummary::kHourSeconds;
constexpr uint32_t kMonthBandSlotSeconds = HourlySummary::kHourSeconds;
constexpr lv_coord_t kRangeButtonClickArea = 12;
constexpr size_t kPeriodTitleCapacity = 48;

// One pass over the month's hourly records feeds both grids.
struct MonthLoad {
    LongChartSeries::Builder *line;
    LongChartSeries::Builder *band;
    int metric;
    uint32_t refused;
};

bool refusedRecord(LongChartSeries::AddResult result) {
    return result == LongChartSeries::AddResult::SourceMismatch ||
           result == LongChartSeries::AddResult::CountOverflow ||
           result == LongChartSeries::AddResult::BadRecord;
}

void addMonthRecord(const QuarterHourRow::SpanRow &row, void *context) {
    MonthLoad &load = *static_cast<MonthLoad *>(context);
    const HistoryAggregate &aggregate = row.metrics[load.metric];
    if (refusedRecord(load.line->add(row.start_epoch, row.end_epoch, aggregate))) ++load.refused;
    if (refusedRecord(load.band->add(row.start_epoch, row.end_epoch, aggregate))) ++load.refused;
}

} // namespace

const UiController::RangeGroup UiController::kRangeGroups[] = {
    {&objects_t::btn_temp_range_month, &objects_t::label_btn_temp_range_month, &UiController::temp_graph_range_,
     &UiController::temp_graph_mode_, &UiController::set_temperature_info_mode,
     [](InfoSensor s) { return s == INFO_TEMP; },
     [](UiController &ui) {
         ui.info_sensor = INFO_TEMP;
         ui.restore_sensor_info_selection();
     }},
    {&objects_t::btn_rh_range_month, &objects_t::label_btn_rh_range_month, &UiController::rh_graph_range_,
     &UiController::rh_graph_mode_, &UiController::set_rh_info_mode,
     [](InfoSensor s) { return s == INFO_RH; },
     [](UiController &ui) { ui.select_humidity_info(INFO_RH); }},
    {&objects_t::btn_voc_range_month, &objects_t::label_btn_voc_range_month, &UiController::voc_graph_range_,
     &UiController::voc_graph_mode_, &UiController::set_voc_info_mode,
     [](InfoSensor s) { return s == INFO_VOC; },
     [](UiController &ui) {
         ui.info_sensor = INFO_VOC;
         ui.restore_sensor_info_selection();
     }},
    {&objects_t::btn_nox_range_month, &objects_t::label_btn_nox_range_month, &UiController::nox_graph_range_,
     &UiController::nox_graph_mode_, &UiController::set_nox_info_mode,
     [](InfoSensor s) { return s == INFO_NOX; },
     [](UiController &ui) {
         ui.info_sensor = INFO_NOX;
         ui.restore_sensor_info_selection();
     }},
    {&objects_t::btn_optional_gas_range_month, &objects_t::label_btn_optional_gas_range_month,
     &UiController::optional_gas_graph_range_, &UiController::optional_gas_graph_mode_,
     &UiController::set_optional_gas_info_mode,
     [](InfoSensor s) { return s == INFO_OPTIONAL_GAS; },
     [](UiController &ui) { ui.select_optional_gas_info(); }},
    {&objects_t::btn_hcho_range_month, &objects_t::label_btn_hcho_range_month, &UiController::hcho_graph_range_,
     &UiController::hcho_graph_mode_, &UiController::set_hcho_info_mode,
     [](InfoSensor s) { return s == INFO_HCHO; },
     [](UiController &ui) {
         ui.info_sensor = INFO_HCHO;
         ui.restore_sensor_info_selection();
     }},
    {&objects_t::btn_co2_range_month, &objects_t::label_btn_co2_range_month, &UiController::co2_graph_range_,
     &UiController::co2_graph_mode_, &UiController::set_co2_info_mode,
     [](InfoSensor s) { return s == INFO_CO2; },
     [](UiController &ui) {
         ui.info_sensor = INFO_CO2;
         ui.restore_sensor_info_selection();
     }},
    {&objects_t::btn_pm05_range_month, &objects_t::label_btn_pm05_range_month, &UiController::pm05_graph_range_,
     &UiController::pm05_graph_mode_, &UiController::set_pm05_info_mode,
     [](InfoSensor s) { return s == INFO_PM05; },
     [](UiController &ui) { ui.select_pm_info(INFO_PM05); }},
    {&objects_t::btn_pm25_4_range_month, &objects_t::label_btn_pm25_4_range_month,
     &UiController::pm25_4_graph_range_, &UiController::pm25_4_graph_mode_, &UiController::set_pm25_4_info_mode,
     [](InfoSensor s) { return s == INFO_PM25 || s == INFO_PM4; },
     [](UiController &ui) { ui.select_pm_info(INFO_PM25); }},
    {&objects_t::btn_pm1_10_range_month, &objects_t::label_btn_pm1_10_range_month,
     &UiController::pm1_10_graph_range_, &UiController::pm1_10_graph_mode_, &UiController::set_pm1_10_info_mode,
     [](InfoSensor s) { return s == INFO_PM1 || s == INFO_PM10; },
     [](UiController &ui) { ui.select_pm_info(INFO_PM10); }},
    {&objects_t::btn_co_range_month, &objects_t::label_btn_co_range_month, &UiController::co_graph_range_,
     &UiController::co_graph_mode_, &UiController::set_co_info_mode,
     [](InfoSensor s) { return s == INFO_CO; },
     [](UiController &ui) { ui.select_pm_info(INFO_CO); }},
    {&objects_t::btn_pressure_range_month, &objects_t::label_btn_pressure_range_month,
     &UiController::pressure_graph_range_, &UiController::pressure_graph_mode_,
     &UiController::set_pressure_info_mode,
     [](InfoSensor s) { return s == INFO_PRESSURE_3H || s == INFO_PRESSURE_24H; },
     [](UiController &ui) { ui.select_pressure_info(INFO_PRESSURE_3H); }},
};
const size_t UiController::kRangeGroupCount = sizeof(kRangeGroups) / sizeof(kRangeGroups[0]);

void UiController::on_range_month_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_long_range_event(e, TEMP_GRAPH_RANGE_MONTH);
}

void UiController::on_chart_period_prev_event_cb(lv_event_t *e) {
    if (instance_ && lv_event_get_code(e) == LV_EVENT_CLICKED) instance_->step_chart_period(ChartPeriod::Step::Back);
}

void UiController::on_chart_period_next_event_cb(lv_event_t *e) {
    if (instance_ && lv_event_get_code(e) == LV_EVENT_CLICKED) {
        instance_->step_chart_period(ChartPeriod::Step::Forward);
    }
}

bool UiController::chart_month_enabled() const {
    return storage.config().charts.month_enabled;
}

const UiController::RangeGroup *UiController::active_range_group() const {
    for (size_t i = 0; i < kRangeGroupCount; ++i) {
        if (kRangeGroups[i].shows(info_sensor)) return &kRangeGroups[i];
    }
    return nullptr;
}

bool UiController::period_chart_active() const {
    const RangeGroup *group = active_range_group();
    return group && this->*group->graph_mode && this->*group->range == TEMP_GRAPH_RANGE_MONTH;
}

void UiController::on_long_range_event(lv_event_t *e, TempGraphRange range) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (index >= kRangeGroupCount) {
        LOGE(kTag, "range button with unknown group %u", static_cast<unsigned>(index));
        return;
    }
    const RangeGroup &group = kRangeGroups[index];
    if (!group.shows(info_sensor)) group.select(*this);
    this->*group.range = range;
    open_month_navigator();
    (this->*group.set_mode)(true);
    update_sensor_info_ui();
}

void UiController::open_month_navigator() {
    int32_t now_month = 0;
    chart_month_nav_valid_ = ChartPeriod::localMonthIndexAt(time(nullptr), now_month);
    if (!chart_month_nav_valid_) return;
    uint32_t first_day = 0;
    const int32_t oldest = longHistory.firstDayKey(first_day) ? ChartPeriod::monthIndexOfDayKey(first_day) : now_month;
    chart_month_nav_ = ChartPeriod::make(oldest, now_month, now_month);
    invalidate_active_graph_refresh_cache();
}

void UiController::step_chart_period(ChartPeriod::Step step) {
    if (!chart_month_nav_valid_ || !ChartPeriod::step(chart_month_nav_, step)) return;
    invalidate_active_graph_refresh_cache();
    update_sensor_info_ui();
}

bool UiController::month_bounds(uint32_t &start, uint32_t &end) const {
    return chart_month_nav_valid_ && ChartPeriod::localMonthBounds(chart_month_nav_.current, start, end);
}

uint16_t UiController::month_line_points() const {
    uint32_t start = 0;
    uint32_t end = 0;
    if (!month_bounds(start, end)) return 0;
    return static_cast<uint16_t>(LongChartSeries::slotsCovering(start, end, kMonthLineSlotSeconds));
}

UiController::PeriodChartStatus UiController::load_month_chart(int metric_id) {
    uint32_t start = 0;
    uint32_t end = 0;
    if (!chart_month_nav_valid_) return PeriodChartStatus::ClockNotSet;
    if (!month_bounds(start, end)) {
        LOGE(kTag, "no local bounds for month %ld", static_cast<long>(chart_month_nav_.current));
        return PeriodChartStatus::Unreadable;
    }
    const bool gas = metric_id == ChartsHistory::METRIC_OPTIONAL_GAS;
    const uint8_t source = gas ? chartsHistory.optionalGasType() : 0;
    // The month in progress gains a record every hour; a past month never changes.
    const time_t now = time(nullptr);
    const uint32_t hour = (now >= static_cast<time_t>(start) && now < static_cast<time_t>(end))
        ? static_cast<uint32_t>(now / HourlySummary::kHourSeconds)
        : 0U;
    PeriodChartCache &cache = period_chart_;
    if (cache.loaded && cache.metric == metric_id && cache.source == source &&
        cache.period == chart_month_nav_.current && cache.hour == hour) {
        return PeriodChartStatus::Ready;
    }

    const LongChartSeries::Grid line_grid{start, kMonthLineSlotSeconds,
                                          LongChartSeries::slotsCovering(start, end, kMonthLineSlotSeconds)};
    const LongChartSeries::Grid band_grid{start, kMonthBandSlotSeconds,
                                          LongChartSeries::slotsCovering(start, end, kMonthBandSlotSeconds)};
    LongChartSeries::Builder line;
    LongChartSeries::Builder band;
    if (!line.reset(line_grid, gas, source) || !band.reset(band_grid, gas, source)) {
        LOGE(kTag, "empty grid for month %ld", static_cast<long>(chart_month_nav_.current));
        return PeriodChartStatus::Unreadable;
    }
    MonthLoad load{&line, &band, metric_id, 0};
    switch (longHistory.hourlyChart(start, end, &addMonthRecord, &load)) {
        case LongHistory::ChartRead::Read:
            break;
        case LongHistory::ChartRead::Off:
            cache.loaded = false;
            return PeriodChartStatus::HistoryOff;
        case LongHistory::ChartRead::Busy:
            return PeriodChartStatus::Busy;
        case LongHistory::ChartRead::Failed:
            cache.loaded = false;
            return PeriodChartStatus::Unreadable;
    }
    if (load.refused > 0) {
        LOGE(kTag, "month chart left out %u invalid hourly records", static_cast<unsigned>(load.refused));
    }
    line.samples(cache.line);
    band.samples(cache.band);
    cache.line_grid = line_grid;
    cache.band_grid = band_grid;
    cache.metric = metric_id;
    cache.source = source;
    cache.period = chart_month_nav_.current;
    cache.hour = hour;
    cache.loaded = true;
    return PeriodChartStatus::Ready;
}

UiController::GraphSeriesStats UiController::populate_period_chart_series(lv_obj_t *chart,
                                                                          lv_chart_series_t *series,
                                                                          uint16_t points,
                                                                          int metric_id,
                                                                          const ChartPlot::Options &options) {
    GraphSeriesStats stats{};
    chart_axis_ = ChartAxis{};
    chart_axis_.kind = ChartAxis::Kind::Days;
    const PeriodChartStatus status = load_month_chart(metric_id);
    switch (status) {
        case PeriodChartStatus::Ready:
            hide_chart_message();
            break;
        case PeriodChartStatus::ClockNotSet:
            show_chart_message(chart, UiText::MsgChartClockNotSet());
            break;
        case PeriodChartStatus::HistoryOff:
            show_chart_message(chart, UiText::MsgChartHistoryOff());
            break;
        case PeriodChartStatus::Unreadable:
            show_chart_message(chart, UiText::MsgChartHistoryUnreadable());
            break;
        case PeriodChartStatus::Busy:
            // A web operation holds the history for a moment: keep what was
            // loaded for this month, otherwise try again on the next refresh.
            hide_chart_message();
            break;
    }
    const PeriodChartCache &cache = period_chart_;
    const bool usable = cache.loaded && cache.metric == metric_id &&
                        cache.source == (metric_id == ChartsHistory::METRIC_OPTIONAL_GAS ? chartsHistory.optionalGasType() : 0) &&
                        cache.period == chart_month_nav_.current &&
                        (status == PeriodChartStatus::Ready || status == PeriodChartStatus::Busy);
    if (!usable || cache.line.size() != points || points < 2U) {
        hide_chart_band();
        return stats;
    }

    chart_line_values_.resize(points);
    ChartPlot::Stats plot = ChartPlot::plotLine(cache.line.data(), points, options, chart_line_values_.data());
    for (uint16_t i = 0; i < points; ++i) {
        lv_chart_set_value_by_id(chart, series, i, chart_line_values_[i]);
    }
    chart_axis_.valid = true;
    chart_axis_.first = LongChartSeries::slotMiddle(cache.line_grid, 0);
    chart_axis_.last = LongChartSeries::slotMiddle(cache.line_grid, points - 1U);
    const size_t band_count = cache.band.size();
    chart_band_points_.resize(band_count);
    ChartPlot::plotBand(cache.band.data(), band_count, LongChartSeries::slotMiddle(cache.band_grid, 0),
                        cache.band_grid.slot_seconds, options, chart_band_points_.data(), plot);
    show_chart_band(chart, chart_axis_.first, chart_axis_.last);

    stats.has_values = plot.has_values;
    stats.min_value = plot.min_value;
    stats.max_value = plot.max_value;
    stats.latest_value = plot.latest_value;
    stats.scale_min = plot.scale_min;
    stats.scale_max = plot.scale_max;
    return stats;
}

void UiController::show_chart_message(lv_obj_t *chart, const char *text) {
    lv_obj_t *container = lv_obj_get_parent(chart);
    if (!chart_message_label_ || !lv_obj_is_valid(chart_message_label_)) {
        chart_message_label_ = lv_label_create(container);
        lv_obj_clear_flag(chart_message_label_,
                          static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_style_text_align(chart_message_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (lv_obj_get_parent(chart_message_label_) != container) {
        lv_obj_set_parent(chart_message_label_, container);
    }
    if (objects.label_sensor_info_title) {
        lv_obj_set_style_text_font(chart_message_label_,
                                   lv_obj_get_style_text_font(objects.label_sensor_info_title, LV_PART_MAIN),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_set_style_text_color(chart_message_label_, active_text_color(), LV_PART_MAIN | LV_STATE_DEFAULT);
    safe_label_set_text(chart_message_label_, text);
    lv_obj_align_to(chart_message_label_, chart, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(chart_message_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(chart_message_label_);
}

void UiController::hide_chart_message() {
    if (chart_message_label_ && lv_obj_is_valid(chart_message_label_)) {
        lv_obj_add_flag(chart_message_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiController::sync_long_range_buttons() {
    const bool month_enabled = chart_month_enabled();
    for (size_t i = 0; i < kRangeGroupCount; ++i) {
        const RangeGroup &group = kRangeGroups[i];
        if (!month_enabled && this->*group.range == TEMP_GRAPH_RANGE_MONTH) {
            // Turned off while on show: back to the default range.
            this->*group.range = TEMP_GRAPH_RANGE_3H;
            invalidate_active_graph_refresh_cache();
        }
        lv_obj_t *button = objects.*group.month_button;
        if (!button) continue;
        lv_obj_add_flag(button, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE));
        lv_obj_set_ext_click_area(button, kRangeButtonClickArea);
        set_visible(button, month_enabled);
        if (this->*group.range == TEMP_GRAPH_RANGE_MONTH) {
            lv_obj_add_state(button, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(button, LV_STATE_CHECKED);
        }
    }
}

void UiController::sync_chart_period_header() {
    const bool pressure_overlay = (info_sensor == INFO_PRESSURE_3H || info_sensor == INFO_PRESSURE_24H) &&
                                  pressure_altitude_overlay_open_;
    const bool show = period_chart_active() && !pressure_overlay;
    if (!show) {
        hide_chart_message();
        set_visible(objects.btn_chart_period_prev, false);
        set_visible(objects.btn_chart_period_next, false);
        if (chart_period_title_ && lv_obj_is_valid(chart_period_title_)) {
            lv_obj_add_flag(chart_period_title_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_t *title = objects.label_sensor_info_title;
    lv_obj_t *prev = objects.btn_chart_period_prev;
    lv_obj_t *next = objects.btn_chart_period_next;
    if (!title || !prev || !next) {
        LOGE(kTag, "chart period header objects missing");
        return;
    }
    // The period title replaces the sensor name between the two arrows.
    lv_obj_t *header = lv_obj_get_parent(title);
    if (!chart_period_title_ || !lv_obj_is_valid(chart_period_title_)) {
        chart_period_title_ = lv_label_create(header);
        lv_obj_clear_flag(chart_period_title_,
                          static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_style_text_align(chart_period_title_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(chart_period_title_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }
    lv_obj_set_style_text_font(chart_period_title_, lv_obj_get_style_text_font(title, LV_PART_MAIN),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(chart_period_title_, lv_obj_get_style_text_color(title, LV_PART_MAIN),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_update_layout(header);
    const lv_coord_t left = lv_obj_get_x(prev) + lv_obj_get_width(prev);
    lv_obj_set_pos(chart_period_title_, left, lv_obj_get_y(title));
    lv_obj_set_width(chart_period_title_, lv_obj_get_x(next) - left);

    const bool clock_set = chart_month_nav_valid_;
    char text[kPeriodTitleCapacity];
    if (!clock_set) {
        safe_label_set_text(chart_period_title_, UiText::LabelChartRangeMonth());
    } else if (ChartPeriod::formatTitle(UiText::LabelChartPeriodTitle(),
                                        UiText::MonthName(ChartPeriod::monthOfYear(chart_month_nav_.current)),
                                        ChartPeriod::monthYear(chart_month_nav_.current), text, sizeof(text))) {
        safe_label_set_text(chart_period_title_, text);
    } else {
        LOGE(kTag, "chart period title template is invalid for this language");
        safe_label_set_text(chart_period_title_, "");
    }
    set_visible(title, false);
    lv_obj_clear_flag(chart_period_title_, LV_OBJ_FLAG_HIDDEN);
    set_visible(prev, clock_set && ChartPeriod::canStep(chart_month_nav_, ChartPeriod::Step::Back));
    set_visible(next, clock_set && ChartPeriod::canStep(chart_month_nav_, ChartPeriod::Step::Forward));
    lv_obj_move_foreground(prev);
    lv_obj_move_foreground(next);
}

void UiController::update_long_range_texts() {
    for (size_t i = 0; i < kRangeGroupCount; ++i) {
        lv_obj_t *label = objects.*kRangeGroups[i].month_label;
        if (label) safe_label_set_text_static(label, UiText::LabelChartRangeMonth());
    }
}

bool UiController::format_day_tick(int64_t epoch, char *buf, size_t len) const {
    const time_t t = static_cast<time_t>(epoch);
    struct tm local {};
    if (!localtime_r(&t, &local)) return false;
    return snprintf(buf, len, "%d", local.tm_mday) > 0;
}
