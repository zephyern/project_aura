# Device chart features

Status: being built as a stack of small pull requests. Every feature here is optional and
ships **off**; nothing changes on the device until its setting is turned on. This file
grows with each pull request in the stack.

The sensor charts on the device's info screen draw the five-minute history
([FIVE_MINUTE_HISTORY.md](FIVE_MINUTE_HISTORY.md)). The longer ranges added later in this
stack read the long-term SD history ([LONG_HISTORY.md](LONG_HISTORY.md)).

## Settings

All chart settings live in one `charts` section of the stored config. The single list of
keys is `ChartsSettings::kFields` in `src/config/ChartsSettings.h`; the config loader, the
saver and the web API all walk that list, so a new setting is one new row there.

| Key | Default | What it turns on |
| --- | --- | --- |
| `band_enabled` | `false` | Faded min/max band on the 1h/3h/24h charts |
| `month_enabled` | `false` | Month range on every sensor chart |
| `year_enabled` | `false` | Year range on every sensor chart |
| `lifetime_enabled` | `false` | Lifetime range ("All") on every sensor chart |
| `multi_enabled` | `false` | Multi-sensor chart, opened by holding a sensor chart |

- A key missing from the stored config keeps its default (off), like every other setting.
- `StorageManager::saveChartsSettings` saves the whole section; when the save fails it puts
  the previous values back, logs `failed to persist chart settings` and returns false.
- A saved change applies at the next chart refresh (within about five seconds); no restart.

### Web API

`GET /api/charts/settings` returns `{"success":true, <every key>}`.

`POST /api/charts/settings` takes a JSON object with **every** key and nothing else, each
`true` or `false`. Anything else is refused with HTTP 400 and an error that names the
problem and lists the accepted keys, for example
`Unknown field: bands (fields: band_enabled)` or `Missing field: band_enabled (fields: ...)`.
During a firmware update it answers 503. A failed save answers 500. On success it returns
the saved values in the same shape as the GET.

The dashboard's System tab has a **Device Charts** group with one checkbox per setting and
a **Save chart settings** button. There is no switch on the device itself.

Host tests: `test/test_web_charts_settings_api_utils`, `test/test_storage_manager_save`
(save, reload, rollback), `test/test_web_route_registry`,
`scripts/tests/dashboard-sd-recovery.test.mjs` (dashboard save/load).

## Faded min/max band (`band_enabled`)

With the setting on, the 1h, 3h and 24h charts draw, under the line, a faded band from the
lowest to the highest reading inside each five-minute interval. The band is the line's own
colour at 40 % opacity (`kChartBandOpa`), so it reads as part of the same series and keeps
the zone colours behind it visible.

- Each five-minute entry already stores its exact low and high (`ChartsHistory::Entry`
  `minimum`/`maximum`). Only full summaries carry them: an entry migrated from the old
  snapshot format has a mean but no range (`ChartsHistory::metricHasRange`), so it shows on
  the line but leaves a gap in the band. The band never invents a range.
- The band breaks wherever the line breaks: a missing interval, a value that is not finite,
  or (for sensors whose values cannot be negative) a negative value, at either end.
- With the band on, the Y axis grows to fit the band as well as the line, so the band is
  never cut off. The MIN/NOW/MAX labels still show the line's values.
- Temperature and pressure go through the same unit conversion as the line (°C/°F,
  hPa/inHg).

How it is built:

- `src/ui/ChartPlot.{h,cpp}` turns history samples into chart values for the line and the
  band in one place: the unit conversion, the point scale and the non-negative rule are
  shared, so the two cannot disagree. Every sensor chart, pressure included, now uses this
  one path (pressure had its own copy of the loop before).
- `src/ui/ChartBand.{h,cpp}` turns band points into filled rectangles. It maps positions
  and values to pixels exactly as LVGL's line chart does (`lv_chart.c`,
  `draw_series_line`: `x = (w * i) / (count - 1)`, values clamped to the axis range), joins
  neighbouring points by straight lines like the chart line, takes the union of all spans
  that fall in the same pixel column, and merges equal neighbouring columns into one
  rectangle. Band points may lie beyond the line's first or last point (the Month view's
  hourly band reaches past the middle of the first and last three-hour slot); the band is
  cut at the chart's first and last pixel column, as LVGL clips the line. It reports `Drawn`, `Empty`, `BadFrame` or `BadPoint`; the UI logs the last two
  as an error, once per chart refresh.
- `UiController` keeps one transparent overlay object, placed directly below the visible
  chart (above the zone colours, below the chart's own background tint, grid and line) and
  sized to it. Its draw callback reads the chart's current axis range and series colour and
  fills the rectangles with `lv_draw_fill`.

Host tests: `test/test_chart_band` (LVGL mapping, interpolation, gaps, crowded columns,
bad input), `test/test_chart_plot` (scaling, conversion, filters, scale widening, entries
without a range), `test/test_charts_history` (`metricValid`, `metricHasRange`).

Not verified on hardware: how readable the band is on the real panels (4.3" and 7"); the
opacity may need tuning after a look on a device.

### Existing solutions checked

- LVGL 9.6 `lv_chart` (bundled, and the docs at
  <https://lvgl.io/docs/open/widgets/chart>): line, bar, stacked and scatter series only;
  no area or range series, and no way to fill between two series.
- LVGL example `examples/widgets/chart/lv_example_chart_area_gradient.c`: fills the area
  *under* one line through draw-task events. It does not fill between two values, and it
  hooks every draw task of the chart, so it was used only as a reference for the draw API.
- The "two filled series" trick used by general charting tools (fill below the max line in
  the band colour, then below the min line in the background colour, e.g.
  <https://help.datylon.com/tutorials/add-reference-band-to-line-chart>): LVGL has no
  fill-below for series, and painting the background colour would erase the zone colours
  and grid under the band.
- Range-area series in other libraries (e.g. LightningChart's `AreaRangeSeries`,
  <https://lightningchart.com/js-charts/docs/features/xy/area-range/>): the right model,
  but not available for LVGL/C, so the small `ChartBand` geometry above implements it.

## Month range (`month_enabled`)

With the setting on, every range group on the info screen (all twelve sensor charts) gets a
fourth button, **Month**, after 24h. Month shows one local calendar month:

- **Line:** one point per three hours, the sample-weighted mean of the three hourly
  summaries in that slot (`HistoryAggregates::combine`, the same merge the history files
  use, so it is the exact mean of every reading, not a mean of means).
- **Band:** one point per hour, from the lowest to the highest reading of that hour. The band
  is part of the Month view and is drawn whatever `band_enabled` says (that setting is about
  the five-minute charts).
- **Data:** the hourly files (`/aura/history/hourly/YYYY-MM.csv`, UTC hours) through
  `LongHistory::hourlyChart`, which takes the long-term history lock, reads every hour that
  starts inside the local month with `HourlySummary::readRange` (it already crosses the
  UTC month files), then adds the hour still in progress. No second reader, no copy of the
  file logic.
- **Header:** the sensor name is replaced by the month ("October 2026", from the language's
  template and month names) between ‹ and › buttons. ‹ stops at the month of the oldest
  record (`LongHistory::firstDayKey`), › stops at the current month: there is no future
  month. An arrow that cannot move is hidden. A title too long for the space scrolls.
- **Time labels:** the day of the month under each of the seven ticks.
- **Messages instead of an empty chart:** "Long-term history is off", "History could not
  be read" (after the read error is logged) and "Clock not set" (no trusted time, so no
  local month). While a web operation holds the history for a moment, the chart keeps what
  it had and retries at the next refresh.
- **Reading cost:** the month is read once and kept; it is read again only when the sensor,
  the month or (for the current month) the hour changes, never on the five-second refresh.
  The optional gas chart shows only records of the gas sensor fitted now.
- Turning the setting off while Month is on screen returns that chart to 3h.

Grids are anchored at local midnight on the 1st (`ChartPeriod::localMonthBounds`, which
asks `mktime` and so follows daylight saving time: a fall-back month has one hour more).
Each hourly record goes to the slot that holds its middle (`LongChartSeries::Builder`), so
in a zone with a half-hour offset the first and last slot of a month may each miss one
hour.

How it is built:

- `src/ui/ChartPeriod.{h,cpp}`: month numbering, the ‹ › limits (`Navigator`), local month
  and year bounds, and the title template (`{month}` and `{year}` exactly once each).
- `src/ui/LongChartSeries.{h,cpp}`: puts history records on a regular grid of slots and
  hands the slots to `ChartPlot` as samples, so the line, band, unit conversion and axis
  code are the ones the short charts use.
- `src/ui/UiControllerLongCharts.cpp`: one table (`UiController::kRangeGroups`) for the
  twelve range groups binds, styles, shows and handles the Month buttons, instead of twelve
  copies of each handler; it also draws the header and the messages.
- The buttons and arrows are EEZ widgets (`ui/aura-lvgl.eez-project`), hidden by default.
- New texts in all 11 languages (10 are machine translations for review): the button, the
  title template, the twelve month names and the three messages. One Chinese glyph was
  added to the 14 px Noto Sans SC subset.

Host tests: `test/test_chart_period` (month numbering, arrow limits, bounds across DST and
year ends, local month of a time, title templates), `test/test_long_chart_series` (slot
counts, exact three-hour merge, gaps, records off the grid, the gas source check),
`test/test_long_history` (`hourlyChart` reads the range and the hour in progress once;
refused when off), `test/test_chart_band` (cutting at the first and last column).

Not verified on hardware: the look of the Month button, header and band on the real panels.

## Year range (`year_enabled`)

With the setting on, every range group gets a **Year** button after Month (each long range
has its own setting, so either can be on alone). Year shows one local calendar year:

- **Line and band:** one point per local day: the day's mean, and a band from its lowest to
  its highest reading. Both come from the daily files (`/aura/history/days/YYYY.csv`, local
  years) through `LongHistory::dailyChart`, which shares one reader with `hourlyChart`
  (`LongHistory::chartRead`: the same lock, the same `Off`/`Busy`/`Failed` answers, records
  limited to the range, then the day still in progress).
- **Header:** the year ("2026") between ‹ and ›; ‹ stops at the year of the oldest record,
  › at the current year.
- **Time labels:** the short month name under each tick, in the language's body font (the
  Chinese and Japanese names are characters).
- The messages, the hourly re-read of the year in progress, the gas sensor check and the
  return to 3h when the setting is turned off work as for Month.

The grid has one 24-hour slot per day from local midnight on January 1st. A local day is
23 to 25 hours long, but a daily record goes to the slot holding its middle, and an hour of
daylight saving shift cannot move a middle out of its slot, so every day keeps its own point.

How it is built: the Month code became a table of long ranges
(`UiController::kLongRangeSpecs`: setting, button text, slot sizes, reader, period bounds,
title and time labels), so Year is one new row and not a second copy. `UiLocalization`
now names the body fonts of each language in one place (`bodyFont14`/`bodyFont18`).
New texts: the button and twelve short month names in all 11 languages (10 machine
translations for review).

Host tests: `test/test_long_history` (`dailyChart` reads the year file and the day in
progress, only inside the range; refused when off), `test/test_long_chart_series` (every
day of a year with both daylight saving changes gets its own slot).

Not verified on hardware: the look of the Year button and month labels on the real panels.

## Lifetime range (`lifetime_enabled`)

With the setting on, every range group gets an **All** button after Year. It shows
everything recorded, from the oldest record to now, from the week ladder
(`/aura/history/weeks/w1.csv`, `w2.csv`, ... `w1024.csv`, UTC weeks from Monday
2026-01-05).

- **Level:** `LifetimeChart::chooseLevels` takes the finest ladder level whose buckets
  from the oldest record to now number at most 300 (`WeeklySummary::kMaxPointsPerLevel`,
  the same bound the ladder keeps per level) for the line, and the next finer level for
  the band (one week when the line is weekly). A year of data is about 52 weekly points;
  after 300 weeks the line becomes two-weekly with a weekly band, and so on.
- **Reading:** only the band level is read, through `LongHistory::weeklyChart`. Two band
  records combine exactly into one line slot (`LongChartSeries::Builder`), so the coarser
  file is not needed. The ladder writes only complete buckets, so `weeklyChart` follows the
  level's file with the one-week records after its last bucket and then the week in
  progress: the chart always reaches the present.
- **Header:** there is no period to step, so the sensor name stays and there are no ‹ ›.
- **Time labels:** year and month (`2026-11`).
- Records before the ladder's anchor (2026-01-05) are not in the ladder; the chart starts
  at the anchor. The messages, the hourly re-read, the gas sensor check and the return to
  3h when the setting is turned off work as for Month.

How it is built: `src/ui/LifetimeChart.{h,cpp}` (the level choice), one more row in
`UiController::kLongRangeSpecs`, `LongHistory::weeklyChart` (the same reader as the hourly
and daily charts) and `LongHistory::firstRecordStart`. The button text is in all 11
languages (10 machine translations for review).

Host tests: `test/test_lifetime_chart` (one week, the 300-point limit, doubling levels and
their alignment, the anchor, impossible times), `test/test_long_history` (`weeklyChart` at
level 1 and at a level with no file yet, an unknown level, `firstRecordStart`; refused
when off).

Not verified on hardware: the look of the All button and the year-month labels.

## Multi-sensor selection model

The rules for choosing what the multi-sensor chart shows, as a pure class with no
screen yet (the screen is the next change): `MultiChartSelection::Selection` in
`src/ui/MultiChartSelection.{h,cpp}`.

- **Grouped by unit.** The series and their units come from the dashboard's chart table
  (`WebChartsUtils`, now walkable with `chartMetricCount` / `chartMetricAt`), so the device
  and the dashboard never disagree. The optional gas takes the unit of the fitted sensor
  (`DfrOptionalGasSensor::unitForType`: ppm, ppb for ozone, %Vol for oxygen) instead of the
  table's fixed ppm, so ozone groups with HCHO (ppb). Groups are listed in the order their
  unit first appears.
- **Two units at most**, one per Y axis. Adding a series of a third unit is refused with
  `Toggle::ThirdUnit` and changes nothing, so the screen can say so plainly. A unit keeps its
  axis while any of its series is selected, so the other unit never jumps sides.
- **Hide and show keep the data.** A hidden series stays selected and keeps its unit's axis;
  only removing it frees the axis.
- **Outdoor temperature (layer E)** will be one more series with the unit `C`, so it shares
  the indoor temperature's axis. `kMaxSeries` is the one place that grows.

Host tests: `test/test_multi_chart_selection` (7 cases), `test/test_web_charts_utils` (+1).

## Multi-sensor chart (`multi_enabled`)

With `multi_enabled` on, **holding** (long press) any sensor chart on the info screen opens a
full-screen chart that draws several sensors together over the last 1h, 3h or 24h. The
chart that was held is added to the lines already chosen (or shown again if it was hidden).
With the setting off a long press does nothing, exactly as before; turning it off while the
screen is open returns to the sensor's own chart.

- **Screen.** `page_multi_chart` (EEZ, last screen id): the title, 1h / 3h / 24h, **LINES**
  and **BACK** in the header; the chart with one Y axis on each side, the unit above each
  axis, times below, and a legend of the chosen lines in colour.
- **Two Y axes.** Each unit gets its own axis (LVGL charts have exactly two: primary and
  secondary Y, which is why two units is the limit). `MultiChartAxis::fit` picks the
  smallest round axis (steps of 1, 2 or 5 times a power of ten) holding every visible line
  of that unit; both axes have four divisions so they share the grid lines, and each gets
  its own point scale and decimals so small values (e.g. 0.012 ppm) keep their detail.
- **LINES** opens the list of every sensor, grouped under its unit, with the unit beside each
  name. Tapping a row adds or removes that line. Picking a third unit is refused and the
  screen says so ("Only two units fit, one per side. Remove the lines of one unit first.");
  nothing is swapped silently.
- **Hide a line** by tapping its legend chip; it fades and its line goes, but it stays chosen
  and its unit keeps its axis, so tapping again brings it straight back.
- **Values** are read exactly as each sensor's own chart reads them
  (`UiController::read_recent_samples`, shared with the single charts) and shown in the
  same display units (°C/°F, hPa/inHg, MSL pressure when an altitude is set). No band here.
- The chosen lines are kept in memory until restart. A different optional gas sensor (which
  can change its unit group) starts the selection again.
- Each sensor has a fixed colour (`kMultiSeriesStyles`), so a line keeps its colour whatever
  else is shown.

Host tests: `test/test_multi_chart_axis` (5 cases). The screen itself is checked by the
firmware build only; its look on the 4.3" and 7" panels has not been checked on a device.
