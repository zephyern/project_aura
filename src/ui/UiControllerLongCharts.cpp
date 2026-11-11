// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

// The long chart ranges (docs/CHARTS_HISTORY.md): the Month, Year and Lifetime
// buttons of every range group, the ‹ › period header and the charts read from the
// long-term history files.

#include "ui/UiController.h"

#include <stdio.h>
#include <time.h>

#include "config/ChartsSettings.h"
#include "core/Logger.h"
#include "modules/ChartsHistory.h"
#include "modules/HourlySummary.h"
#include "modules/LongHistory.h"
#include "ui/LifetimeChart.h"
#include "ui/UiLocalization.h"
#include "ui/UiText.h"
#include "ui/ui.h"

namespace {

constexpr const char *kTag = "Charts";
// Month view: the line has one point per three hours, the band one per hour.
constexpr uint32_t kMonthLineSlotSeconds = 3U * HourlySummary::kHourSeconds;
constexpr uint32_t kMonthBandSlotSeconds = HourlySummary::kHourSeconds;
// Year view: one point per day for both. A local day is 23-25 hours, but a
// daily record goes to the slot holding its middle, which a daylight saving
// shift of one hour cannot move out of its 24-hour slot.
constexpr uint32_t kYearSlotSeconds = 24U * HourlySummary::kHourSeconds;
constexpr lv_coord_t kRangeButtonClickArea = 12;
constexpr size_t kPeriodTitleCapacity = 48;
constexpr int kTmYearBase = 1900;

bool monthTitle(int32_t month_index, char *out, size_t len) {
    return ChartPeriod::formatTitle(UiText::LabelChartPeriodTitle(),
                                    UiText::MonthName(ChartPeriod::monthOfYear(month_index)),
                                    ChartPeriod::monthYear(month_index), out, len);
}

bool yearTitle(int32_t year, char *out, size_t len) {
    const int written = snprintf(out, len, "%ld", static_cast<long>(year));
    return written > 0 && static_cast<size_t>(written) < len;
}

int32_t yearOfDayKey(uint32_t day_key) { return ChartPeriod::yearOfDayKey(day_key); }
int32_t monthOfDayKey(uint32_t day_key) { return ChartPeriod::monthIndexOfDayKey(day_key); }
// Lifetime has a single period, 0, once the clock is trusted.
bool lifetimeAt(time_t epoch, int32_t &period) {
    int32_t year = 0;
    if (!ChartPeriod::localYearAt(epoch, year)) return false;
    period = 0;
    return true;
}
int32_t lifetimeOfDayKey(uint32_t) { return 0; }

// One pass over the period's records feeds both grids.
struct PeriodLoad {
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

void addPeriodRecord(const QuarterHourRow::SpanRow &row, void *context) {
    PeriodLoad &load = *static_cast<PeriodLoad *>(context);
    const HistoryAggregate &aggregate = row.metrics[load.metric];
    if (refusedRecord(load.line->add(row.start_epoch, row.end_epoch, aggregate))) ++load.refused;
    if (refusedRecord(load.band->add(row.start_epoch, row.end_epoch, aggregate))) ++load.refused;
}

} // namespace

struct UiController::LongRangeSpec {
    TempGraphRange graph_range;
    bool ChartsSettings::Values::*enabled;
    const char *(*button_text)();
    const char *name; // for the log
    // [start, end) and slot sizes of `period` (Lifetime: from `first` to `now`).
    bool (*layout)(int32_t period, uint32_t first, time_t now, LongChartSeries::Layout &out);
    LongHistory::ChartRead (*read)(const LongHistory &history, const LongChartSeries::Layout &layout,
                                   LongHistory::ChartRecordFn fn, void *context);
    bool (*period_at)(time_t epoch, int32_t &period);
    int32_t (*period_of_day)(uint32_t day_key);
    // Title between ‹ ›; nullptr for a range without periods (no header).
    bool (*title)(int32_t period, char *out, size_t len);
    ChartAxis::Kind ticks;
};

namespace {

bool monthLayout(int32_t month_index, uint32_t, time_t, LongChartSeries::Layout &out) {
    out.line_slot_seconds = kMonthLineSlotSeconds;
    out.band_slot_seconds = kMonthBandSlotSeconds;
    return ChartPeriod::localMonthBounds(month_index, out.start, out.end);
}

bool yearLayout(int32_t year, uint32_t, time_t, LongChartSeries::Layout &out) {
    out.line_slot_seconds = kYearSlotSeconds;
    out.band_slot_seconds = kYearSlotSeconds;
    return ChartPeriod::localYearBounds(year, out.start, out.end);
}

bool lifetimeLayout(int32_t, uint32_t first, time_t now, LongChartSeries::Layout &out) {
    LifetimeChart::Levels levels;
    if (now < 0 || !LifetimeChart::chooseLevels(first, static_cast<uint32_t>(now), levels)) return false;
    out.start = levels.start;
    out.end = levels.end;
    out.line_slot_seconds = levels.line_weeks * WeeklySummary::kWeekSeconds;
    out.band_slot_seconds = levels.band_weeks * WeeklySummary::kWeekSeconds;
    out.band_weeks = levels.band_weeks;
    return true;
}

LongHistory::ChartRead readHourly(const LongHistory &history, const LongChartSeries::Layout &l,
                              LongHistory::ChartRecordFn fn, void *ctx) {
    return history.hourlyChart(l.start, l.end, fn, ctx);
}

LongHistory::ChartRead readDaily(const LongHistory &history, const LongChartSeries::Layout &l,
                              LongHistory::ChartRecordFn fn, void *ctx) {
    return history.dailyChart(l.start, l.end, fn, ctx);
}

// The band level's records feed both grids: two of them combine exactly into
// one line slot, so the coarser ladder file is not needed.
LongHistory::ChartRead readWeekly(const LongHistory &history, const LongChartSeries::Layout &l,
                              LongHistory::ChartRecordFn fn, void *ctx) {
    return history.weeklyChart(l.band_weeks, l.start, l.end, fn, ctx);
}

} // namespace

// Indexed by LongRange.
const UiController::LongRangeSpec UiController::kLongRangeSpecs[kLongRangeCount] = {
    {TEMP_GRAPH_RANGE_MONTH, &ChartsSettings::Values::month_enabled, &UiText::LabelChartRangeMonth, "month",
     &monthLayout, &readHourly, &ChartPeriod::localMonthIndexAt, &monthOfDayKey, &monthTitle,
     ChartAxis::Kind::Days},
    {TEMP_GRAPH_RANGE_YEAR, &ChartsSettings::Values::year_enabled, &UiText::LabelChartRangeYear, "year",
     &yearLayout, &readDaily, &ChartPeriod::localYearAt, &yearOfDayKey, &yearTitle, ChartAxis::Kind::Months},
    {TEMP_GRAPH_RANGE_LIFETIME, &ChartsSettings::Values::lifetime_enabled, &UiText::LabelChartRangeLifetime,
     "lifetime", &lifetimeLayout, &readWeekly, &lifetimeAt, &lifetimeOfDayKey, nullptr,
     ChartAxis::Kind::YearMonths},
};

bool UiController::long_range_of(TempGraphRange graph_range, LongRange &range) {
    for (size_t i = 0; i < kLongRangeCount; ++i) {
        if (kLongRangeSpecs[i].graph_range == graph_range) {
            range = static_cast<LongRange>(i);
            return true;
        }
    }
    return false;
}

const UiController::RangeGroup UiController::kRangeGroups[] = {
    {{{&objects_t::btn_temp_range_month, &objects_t::label_btn_temp_range_month},
      {&objects_t::btn_temp_range_year, &objects_t::label_btn_temp_range_year},
      {&objects_t::btn_temp_range_all, &objects_t::label_btn_temp_range_all}},
     &UiController::temp_graph_range_,
     &UiController::temp_graph_mode_, &UiController::set_temperature_info_mode,
     [](InfoSensor s) { return s == INFO_TEMP; },
     [](UiController &ui) {
         ui.info_sensor = INFO_TEMP;
         ui.restore_sensor_info_selection();
     }},
    {{{&objects_t::btn_rh_range_month, &objects_t::label_btn_rh_range_month},
      {&objects_t::btn_rh_range_year, &objects_t::label_btn_rh_range_year},
      {&objects_t::btn_rh_range_all, &objects_t::label_btn_rh_range_all}},
     &UiController::rh_graph_range_,
     &UiController::rh_graph_mode_, &UiController::set_rh_info_mode,
     [](InfoSensor s) { return s == INFO_RH; },
     [](UiController &ui) { ui.select_humidity_info(INFO_RH); }},
    {{{&objects_t::btn_voc_range_month, &objects_t::label_btn_voc_range_month},
      {&objects_t::btn_voc_range_year, &objects_t::label_btn_voc_range_year},
      {&objects_t::btn_voc_range_all, &objects_t::label_btn_voc_range_all}},
     &UiController::voc_graph_range_,
     &UiController::voc_graph_mode_, &UiController::set_voc_info_mode,
     [](InfoSensor s) { return s == INFO_VOC; },
     [](UiController &ui) {
         ui.info_sensor = INFO_VOC;
         ui.restore_sensor_info_selection();
     }},
    {{{&objects_t::btn_nox_range_month, &objects_t::label_btn_nox_range_month},
      {&objects_t::btn_nox_range_year, &objects_t::label_btn_nox_range_year},
      {&objects_t::btn_nox_range_all, &objects_t::label_btn_nox_range_all}},
     &UiController::nox_graph_range_,
     &UiController::nox_graph_mode_, &UiController::set_nox_info_mode,
     [](InfoSensor s) { return s == INFO_NOX; },
     [](UiController &ui) {
         ui.info_sensor = INFO_NOX;
         ui.restore_sensor_info_selection();
     }},
    {{{&objects_t::btn_optional_gas_range_month, &objects_t::label_btn_optional_gas_range_month},
      {&objects_t::btn_optional_gas_range_year, &objects_t::label_btn_optional_gas_range_year},
      {&objects_t::btn_optional_gas_range_all, &objects_t::label_btn_optional_gas_range_all}},
     &UiController::optional_gas_graph_range_, &UiController::optional_gas_graph_mode_,
     &UiController::set_optional_gas_info_mode,
     [](InfoSensor s) { return s == INFO_OPTIONAL_GAS; },
     [](UiController &ui) { ui.select_optional_gas_info(); }},
    {{{&objects_t::btn_hcho_range_month, &objects_t::label_btn_hcho_range_month},
      {&objects_t::btn_hcho_range_year, &objects_t::label_btn_hcho_range_year},
      {&objects_t::btn_hcho_range_all, &objects_t::label_btn_hcho_range_all}},
     &UiController::hcho_graph_range_,
     &UiController::hcho_graph_mode_, &UiController::set_hcho_info_mode,
     [](InfoSensor s) { return s == INFO_HCHO; },
     [](UiController &ui) {
         ui.info_sensor = INFO_HCHO;
         ui.restore_sensor_info_selection();
     }},
    {{{&objects_t::btn_co2_range_month, &objects_t::label_btn_co2_range_month},
      {&objects_t::btn_co2_range_year, &objects_t::label_btn_co2_range_year},
      {&objects_t::btn_co2_range_all, &objects_t::label_btn_co2_range_all}},
     &UiController::co2_graph_range_,
     &UiController::co2_graph_mode_, &UiController::set_co2_info_mode,
     [](InfoSensor s) { return s == INFO_CO2; },
     [](UiController &ui) {
         ui.info_sensor = INFO_CO2;
         ui.restore_sensor_info_selection();
     }},
    {{{&objects_t::btn_pm05_range_month, &objects_t::label_btn_pm05_range_month},
      {&objects_t::btn_pm05_range_year, &objects_t::label_btn_pm05_range_year},
      {&objects_t::btn_pm05_range_all, &objects_t::label_btn_pm05_range_all}},
     &UiController::pm05_graph_range_,
     &UiController::pm05_graph_mode_, &UiController::set_pm05_info_mode,
     [](InfoSensor s) { return s == INFO_PM05; },
     [](UiController &ui) { ui.select_pm_info(INFO_PM05); }},
    {{{&objects_t::btn_pm25_4_range_month, &objects_t::label_btn_pm25_4_range_month},
      {&objects_t::btn_pm25_4_range_year, &objects_t::label_btn_pm25_4_range_year},
      {&objects_t::btn_pm25_4_range_all, &objects_t::label_btn_pm25_4_range_all}},
     &UiController::pm25_4_graph_range_, &UiController::pm25_4_graph_mode_, &UiController::set_pm25_4_info_mode,
     [](InfoSensor s) { return s == INFO_PM25 || s == INFO_PM4; },
     [](UiController &ui) { ui.select_pm_info(INFO_PM25); }},
    {{{&objects_t::btn_pm1_10_range_month, &objects_t::label_btn_pm1_10_range_month},
      {&objects_t::btn_pm1_10_range_year, &objects_t::label_btn_pm1_10_range_year},
      {&objects_t::btn_pm1_10_range_all, &objects_t::label_btn_pm1_10_range_all}},
     &UiController::pm1_10_graph_range_, &UiController::pm1_10_graph_mode_, &UiController::set_pm1_10_info_mode,
     [](InfoSensor s) { return s == INFO_PM1 || s == INFO_PM10; },
     [](UiController &ui) { ui.select_pm_info(INFO_PM10); }},
    {{{&objects_t::btn_co_range_month, &objects_t::label_btn_co_range_month},
      {&objects_t::btn_co_range_year, &objects_t::label_btn_co_range_year},
      {&objects_t::btn_co_range_all, &objects_t::label_btn_co_range_all}},
     &UiController::co_graph_range_,
     &UiController::co_graph_mode_, &UiController::set_co_info_mode,
     [](InfoSensor s) { return s == INFO_CO; },
     [](UiController &ui) { ui.select_pm_info(INFO_CO); }},
    {{{&objects_t::btn_pressure_range_month, &objects_t::label_btn_pressure_range_month},
      {&objects_t::btn_pressure_range_year, &objects_t::label_btn_pressure_range_year},
      {&objects_t::btn_pressure_range_all, &objects_t::label_btn_pressure_range_all}},
     &UiController::pressure_graph_range_, &UiController::pressure_graph_mode_,
     &UiController::set_pressure_info_mode,
     [](InfoSensor s) { return s == INFO_PRESSURE_3H || s == INFO_PRESSURE_24H; },
     [](UiController &ui) { ui.select_pressure_info(INFO_PRESSURE_3H); }},
};
const size_t UiController::kRangeGroupCount = sizeof(kRangeGroups) / sizeof(kRangeGroups[0]);

void UiController::on_long_range_event_cb(lv_event_t *e) {
    if (instance_) instance_->on_long_range_event(e);
}

void UiController::on_chart_period_prev_event_cb(lv_event_t *e) {
    if (instance_ && lv_event_get_code(e) == LV_EVENT_CLICKED) instance_->step_chart_period(ChartPeriod::Step::Back);
}

void UiController::on_chart_period_next_event_cb(lv_event_t *e) {
    if (instance_ && lv_event_get_code(e) == LV_EVENT_CLICKED) {
        instance_->step_chart_period(ChartPeriod::Step::Forward);
    }
}

bool UiController::long_range_enabled(LongRange range) const {
    return storage.config().charts.*kLongRangeSpecs[static_cast<size_t>(range)].enabled;
}

const UiController::RangeGroup *UiController::active_range_group() const {
    for (size_t i = 0; i < kRangeGroupCount; ++i) {
        if (kRangeGroups[i].shows(info_sensor)) return &kRangeGroups[i];
    }
    return nullptr;
}

bool UiController::active_long_range(LongRange &range) const {
    const RangeGroup *group = active_range_group();
    return group && this->*group->graph_mode && long_range_of(this->*group->range, range);
}

void UiController::on_long_range_event(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    // User data: group index * kLongRangeCount + long range (UiEventBinder).
    const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (index >= kRangeGroupCount * kLongRangeCount) {
        LOGE(kTag, "range button with unknown index %u", static_cast<unsigned>(index));
        return;
    }
    const RangeGroup &group = kRangeGroups[index / kLongRangeCount];
    const LongRange range = static_cast<LongRange>(index % kLongRangeCount);
    if (!group.shows(info_sensor)) group.select(*this);
    this->*group.range = kLongRangeSpecs[static_cast<size_t>(range)].graph_range;
    open_period_navigator(range);
    (this->*group.set_mode)(true);
    update_sensor_info_ui();
}

void UiController::open_period_navigator(LongRange range) {
    const LongRangeSpec &spec = kLongRangeSpecs[static_cast<size_t>(range)];
    PeriodNav &nav = period_navs_[static_cast<size_t>(range)];
    int32_t now_period = 0;
    nav.valid = spec.period_at(time(nullptr), now_period);
    invalidate_active_graph_refresh_cache();
    if (!nav.valid) return;
    if (!spec.title) {
        // No periods: the chart runs from the oldest record (or from now
        // when nothing is recorded yet) to the present.
        uint32_t first = 0;
        nav.first = longHistory.firstRecordStart(first) ? first : static_cast<uint32_t>(time(nullptr));
        nav.nav = ChartPeriod::make(now_period, now_period, now_period);
        return;
    }
    uint32_t first_day = 0;
    const int32_t oldest = longHistory.firstDayKey(first_day) ? spec.period_of_day(first_day) : now_period;
    nav.nav = ChartPeriod::make(oldest, now_period, now_period);
}

void UiController::step_chart_period(ChartPeriod::Step step) {
    LongRange range = LongRange::Month;
    if (!active_long_range(range)) return;
    PeriodNav &nav = period_navs_[static_cast<size_t>(range)];
    if (!nav.valid || !ChartPeriod::step(nav.nav, step)) return;
    invalidate_active_graph_refresh_cache();
    update_sensor_info_ui();
}

bool UiController::period_layout(LongRange range, LongChartSeries::Layout &layout) const {
    const PeriodNav &nav = period_navs_[static_cast<size_t>(range)];
    layout = LongChartSeries::Layout{};
    return nav.valid &&
           kLongRangeSpecs[static_cast<size_t>(range)].layout(nav.nav.current, nav.first, time(nullptr), layout);
}

uint16_t UiController::period_line_points(LongRange range) const {
    LongChartSeries::Layout layout;
    if (!period_layout(range, layout)) return 0;
    return static_cast<uint16_t>(LongChartSeries::slotsCovering(layout.start, layout.end, layout.line_slot_seconds));
}

UiController::PeriodChartStatus UiController::load_period_chart(LongRange range, int metric_id) {
    const LongRangeSpec &spec = kLongRangeSpecs[static_cast<size_t>(range)];
    const PeriodNav &nav = period_navs_[static_cast<size_t>(range)];
    if (!nav.valid) return PeriodChartStatus::ClockNotSet;
    LongChartSeries::Layout layout;
    if (!period_layout(range, layout)) {
        LOGE(kTag, "no time span for %s %ld", spec.name, static_cast<long>(nav.nav.current));
        return PeriodChartStatus::Unreadable;
    }
    const uint32_t start = layout.start;
    const uint32_t end = layout.end;
    const bool gas = metric_id == ChartsHistory::METRIC_OPTIONAL_GAS;
    const uint8_t source = gas ? chartsHistory.optionalGasType() : 0;
    // The period in progress is read again each hour; a past one never changes.
    const time_t now = time(nullptr);
    const uint32_t hour = (now >= static_cast<time_t>(start) && now < static_cast<time_t>(end))
        ? static_cast<uint32_t>(now / HourlySummary::kHourSeconds)
        : 0U;
    PeriodChartCache &cache = period_chart_;
    if (cache.loaded && cache.range == range && cache.metric == metric_id && cache.source == source &&
        cache.period == nav.nav.current && cache.hour == hour) {
        return PeriodChartStatus::Ready;
    }

    const LongChartSeries::Grid line_grid{start, layout.line_slot_seconds,
                                          LongChartSeries::slotsCovering(start, end, layout.line_slot_seconds)};
    const LongChartSeries::Grid band_grid{start, layout.band_slot_seconds,
                                          LongChartSeries::slotsCovering(start, end, layout.band_slot_seconds)};
    LongChartSeries::Builder line;
    LongChartSeries::Builder band;
    if (!line.reset(line_grid, gas, source) || !band.reset(band_grid, gas, source)) {
        LOGE(kTag, "empty grid for %s %ld", spec.name, static_cast<long>(nav.nav.current));
        return PeriodChartStatus::Unreadable;
    }
    PeriodLoad load{&line, &band, metric_id, 0};
    switch (spec.read(longHistory, layout, &addPeriodRecord, &load)) {
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
        LOGE(kTag, "%s chart left out %u invalid records", spec.name, static_cast<unsigned>(load.refused));
    }
    line.samples(cache.line);
    band.samples(cache.band);
    cache.line_grid = line_grid;
    cache.band_grid = band_grid;
    cache.range = range;
    cache.metric = metric_id;
    cache.source = source;
    cache.period = nav.nav.current;
    cache.hour = hour;
    cache.loaded = true;
    return PeriodChartStatus::Ready;
}

UiController::GraphSeriesStats UiController::populate_period_chart_series(lv_obj_t *chart,
                                                                          lv_chart_series_t *series,
                                                                          uint16_t points,
                                                                          LongRange range,
                                                                          int metric_id,
                                                                          const ChartPlot::Options &options) {
    GraphSeriesStats stats{};
    chart_axis_ = ChartAxis{};
    chart_axis_.kind = kLongRangeSpecs[static_cast<size_t>(range)].ticks;
    const PeriodChartStatus status = load_period_chart(range, metric_id);
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
            // loaded for this period, otherwise try again on the next refresh.
            hide_chart_message();
            break;
    }
    const PeriodChartCache &cache = period_chart_;
    const bool usable = cache.loaded && cache.range == range && cache.metric == metric_id &&
                        cache.source == (metric_id == ChartsHistory::METRIC_OPTIONAL_GAS ? chartsHistory.optionalGasType() : 0) &&
                        cache.period == period_navs_[static_cast<size_t>(range)].nav.current &&
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
    for (size_t r = 0; r < kLongRangeCount; ++r) {
        const LongRange range = static_cast<LongRange>(r);
        const TempGraphRange graph_range = kLongRangeSpecs[r].graph_range;
        const bool enabled = long_range_enabled(range);
        for (size_t i = 0; i < kRangeGroupCount; ++i) {
            const RangeGroup &group = kRangeGroups[i];
            if (!enabled && this->*group.range == graph_range) {
                // Turned off while on show: back to the default range.
                this->*group.range = TEMP_GRAPH_RANGE_3H;
                invalidate_active_graph_refresh_cache();
            }
            lv_obj_t *button = objects.*group.long_buttons[r].button;
            if (!button) continue;
            lv_obj_add_flag(button, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE));
            lv_obj_set_ext_click_area(button, kRangeButtonClickArea);
            set_visible(button, enabled);
            if (this->*group.range == graph_range) {
                lv_obj_add_state(button, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(button, LV_STATE_CHECKED);
            }
        }
    }
}

void UiController::sync_chart_period_header() {
    const bool pressure_overlay = (info_sensor == INFO_PRESSURE_3H || info_sensor == INFO_PRESSURE_24H) &&
                                  pressure_altitude_overlay_open_;
    LongRange range = LongRange::Month;
    const bool long_active = active_long_range(range);
    const bool show = long_active && kLongRangeSpecs[static_cast<size_t>(range)].title && !pressure_overlay;
    if (!show) {
        if (!long_active || pressure_overlay) hide_chart_message();
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

    const LongRangeSpec &spec = kLongRangeSpecs[static_cast<size_t>(range)];
    const PeriodNav &nav = period_navs_[static_cast<size_t>(range)];
    const bool clock_set = nav.valid;
    char text[kPeriodTitleCapacity];
    if (!clock_set) {
        safe_label_set_text(chart_period_title_, spec.button_text());
    } else if (spec.title(nav.nav.current, text, sizeof(text))) {
        safe_label_set_text(chart_period_title_, text);
    } else {
        LOGE(kTag, "%s title does not fit or its template is invalid for this language", spec.name);
        safe_label_set_text(chart_period_title_, "");
    }
    set_visible(title, false);
    lv_obj_clear_flag(chart_period_title_, LV_OBJ_FLAG_HIDDEN);
    set_visible(prev, clock_set && ChartPeriod::canStep(nav.nav, ChartPeriod::Step::Back));
    set_visible(next, clock_set && ChartPeriod::canStep(nav.nav, ChartPeriod::Step::Forward));
    lv_obj_move_foreground(prev);
    lv_obj_move_foreground(next);
}

void UiController::update_long_range_texts() {
    for (size_t r = 0; r < kLongRangeCount; ++r) {
        for (size_t i = 0; i < kRangeGroupCount; ++i) {
            lv_obj_t *label = objects.*kRangeGroups[i].long_buttons[r].label;
            if (label) safe_label_set_text_static(label, kLongRangeSpecs[r].button_text());
        }
    }
}

bool UiController::format_period_tick(int64_t epoch, char *buf, size_t len) const {
    const time_t t = static_cast<time_t>(epoch);
    struct tm local {};
    if (!localtime_r(&t, &local)) return false;
    int written = 0;
    switch (chart_axis_.kind) {
        case ChartAxis::Kind::Months:
            written = snprintf(buf, len, "%s", UiText::MonthShortName(local.tm_mon + 1));
            break;
        case ChartAxis::Kind::YearMonths:
            written = snprintf(buf, len, "%04d-%02d", local.tm_year + kTmYearBase, local.tm_mon + 1);
            break;
        case ChartAxis::Kind::Days:
        case ChartAxis::Kind::Recent:
            written = snprintf(buf, len, "%d", local.tm_mday);
            break;
    }
    return written > 0 && static_cast<size_t>(written) < len;
}
