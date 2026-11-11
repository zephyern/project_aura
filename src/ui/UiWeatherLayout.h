// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>

#include <lvgl.h>

// Main-screen layout while outdoor weather is on (docs/WEATHER.md). The
// generated screen (EEZ) is the weather-off layout; apply(true) moves cards
// into the weather layout and apply(false) puts back every position, size,
// font and hidden flag it changed, exactly as generated.
//
//   row 1: CO2 (15, two wide) · indoor temp (341) · outdoor (493) · humidity (645)
//   row 2: compact pressure (15) · MR/DP (167) · PM0.5 · PM2.5 · PM10
//
// The compact pressure card keeps its value and unit and shows the 3-hour
// change as one line; the 24-hour change moves to the pressure info screen.
// Its title and value use smaller fonts there (the longest title,
// "PRESSIONE MSL", is wider than one slot at 18 px).
namespace UiWeatherLayout {

struct Fonts {
    const lv_font_t *title_on;   // the language's 14 px body font
    const lv_font_t *title_off;  // the language's 18 px body font (as generated)
};
void apply(bool weather_on, const Fonts &fonts);

// One geometry item, exposed so the offline renderer can check that the
// weather-off values match the generated screen.
struct Item {
    const char *name;
    int off_x, off_y, off_w;  // generated geometry (off_w < 0: width not changed)
    int on_x, on_y, on_w;
    bool hide_when_on;
};
const Item *items(size_t &count);

}  // namespace UiWeatherLayout
