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
  rectangle. It reports `Drawn`, `Empty`, `BadFrame` or `BadPoint`; the UI logs the last two
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
