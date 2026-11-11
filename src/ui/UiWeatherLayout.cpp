// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/UiWeatherLayout.h"

#include <lvgl.h>

#include "ui/fonts.h"
#include "ui/screens.h"

namespace UiWeatherLayout {
namespace {

struct Placed {
    lv_obj_t *objects_t::*object;
    Item item;
};

// Weather-off values are those of ui/aura-lvgl.eez-project (checked by the
// offline renderer, tools/tests/lvgl9_ui).
const Placed kPlaced[] = {
    {&objects_t::card_hum_pro, {"card_hum_pro", 493, 86, -1, 645, 86, -1, false}},
    {&objects_t::card_hum_2, {"card_hum_2", 645, 86, -1, 167, 216, -1, false}},
    {&objects_t::card_pressure_pro, {"card_pressure_pro", 15, 216, 314, 15, 216, 140, false}},
    {&objects_t::label_pressure_title_1, {"label_pressure_title_1", 12, 12, -1, 8, 8, -1, false}},
    {&objects_t::label_pressure_value_1, {"label_pressure_value_1", 12, 45, 122, 8, 36, 96, false}},
    {&objects_t::label_pressure_unit_1, {"label_pressure_unit_1", 135, 63, -1, 107, 50, -1, false}},
    {&objects_t::chip_delta_4, {"chip_delta_4", 176, 13, 122, 9, 74, 122, false}},
    {&objects_t::label_delta_4, {"label_delta_4", 194, 21, -1, 27, 82, -1, false}},
    {&objects_t::label_delta_5, {"label_delta_5", 228, 21, 57, 61, 82, 57, false}},
    {&objects_t::chip_delta_25, {"chip_delta_25", 176, 62, 122, 176, 62, 122, true}},
    {&objects_t::label_delta_25, {"label_delta_25", 186, 70, -1, 186, 70, -1, true}},
    {&objects_t::label_delta_26, {"label_delta_26", 228, 70, 57, 228, 70, 57, true}},
};
constexpr size_t kPlacedCount = sizeof(kPlaced) / sizeof(kPlaced[0]);
Item g_items[kPlacedCount];

}  // namespace

void apply(bool on, const Fonts &fonts) {
    for (const Placed &p : kPlaced) {
        lv_obj_t *obj = objects.*(p.object);
        if (!obj) continue;
        const Item &it = p.item;
        lv_obj_set_pos(obj, on ? it.on_x : it.off_x, on ? it.on_y : it.off_y);
        if (it.off_w >= 0) lv_obj_set_width(obj, on ? it.on_w : it.off_w);
        if (it.hide_when_on) {
            if (on) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // Four digits of the 48 px value do not fit beside the unit in one slot.
    if (objects.label_pressure_value_1) {
        lv_obj_set_style_text_font(objects.label_pressure_value_1, on ? &ui_font_jet_med_28 : &ui_font_jet_med_48,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (objects.label_pressure_title_1) {
        lv_obj_set_style_text_font(objects.label_pressure_title_1, on ? fonts.title_on : fonts.title_off,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (objects.card_outdoor_pro) {
        if (on) lv_obj_remove_flag(objects.card_outdoor_pro, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(objects.card_outdoor_pro, LV_OBJ_FLAG_HIDDEN);
    }
}

const Item *items(size_t &count) {
    for (size_t i = 0; i < kPlacedCount; ++i) g_items[i] = kPlaced[i].item;
    count = kPlacedCount;
    return g_items;
}

}  // namespace UiWeatherLayout
