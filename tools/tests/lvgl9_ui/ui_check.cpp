// SPDX-FileCopyrightText: 2026 Volodymyr Papush (21CNCStudio)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvgl.h>
#include "ui/screens.h"
#include "ui/ThemeColorStorage.h"
#include "ui/UiCo2CardPresentation.h"
#include "ui/UiOutdoorCard.h"
#include "ui/UiWeatherLayout.h"
#include "ui/fonts.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

static int flushed = 0;
static uint16_t *presented = nullptr;
static void flush(lv_display_t *display, const lv_area_t *, uint8_t *pixels) {
    if (lv_display_flush_is_last(display)) {
        presented = reinterpret_cast<uint16_t *>(pixels);
        ++flushed;
    }
    lv_display_flush_ready(display);
}
static void log_message(lv_log_level_t level, const char *message) {
    std::fputs(message, stderr);
    if (level >= LV_LOG_LEVEL_ERROR) std::abort();
}

static const UiWeatherLayout::Fonts kFonts{&ui_font_jet_reg_14, &ui_font_jet_reg_18};

static void check_weather_off_geometry() {
    size_t count = 0;
    const UiWeatherLayout::Item *items = UiWeatherLayout::items(count);
    lv_obj_t *objs[] = {objects.card_hum_pro, objects.card_hum_2, objects.card_pressure_pro,
        objects.label_pressure_title_1, objects.label_pressure_value_1, objects.label_pressure_unit_1,
        objects.chip_delta_4, objects.label_delta_4, objects.label_delta_5, objects.chip_delta_25,
        objects.label_delta_25, objects.label_delta_26};
    assert(count == sizeof(objs) / sizeof(*objs));
    for (size_t i = 0; i < count; ++i) {
        const int x = lv_obj_get_x_aligned(objs[i]), y = lv_obj_get_y_aligned(objs[i]);
        if (x != items[i].off_x || y != items[i].off_y ||
            (items[i].off_w >= 0 && lv_obj_get_style_width(objs[i], LV_PART_MAIN) != items[i].off_w)) {
            std::fprintf(stderr, "weather-off geometry of %s differs from the generated screen\n", items[i].name);
            std::abort();
        }
    }
}

// A value label shows its text on one line (no wrap inside its width).
static void check_single_line(lv_obj_t *label) {
    lv_obj_update_layout(label);
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (lv_obj_get_content_height(label) > lv_font_get_line_height(font)) {
        std::fprintf(stderr, "value \"%s\" wraps\n", lv_label_get_text(label));
        std::abort();
    }
}

// Every visible child lies inside its card once laid out.
static void check_inside_card(lv_obj_t *card) {
    lv_obj_update_layout(card);
    const int w = lv_obj_get_width(card), h = lv_obj_get_height(card);
    for (uint32_t i = 0; i < lv_obj_get_child_count(card); ++i) {
        lv_obj_t *child = lv_obj_get_child(card, static_cast<int32_t>(i));
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t a;
        lv_obj_get_coords(child, &a);
        lv_area_t c;
        lv_obj_get_coords(card, &c);
        if (a.x1 < c.x1 || a.y1 < c.y1 || a.x2 > c.x1 + w - 1 || a.y2 > c.y1 + h - 1) {
            std::fprintf(stderr, "child %u (%d,%d)-(%d,%d) of a weather card (%d,%d %dx%d) is outside it\n",
                         static_cast<unsigned>(i), static_cast<int>(a.x1), static_cast<int>(a.y1),
                         static_cast<int>(a.x2), static_cast<int>(a.y2), static_cast<int>(c.x1),
                         static_cast<int>(c.y1), w, h);
            std::abort();
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::filesystem::create_directories(argv[1]);
    // Every old RGB565 preference must survive a LVGL 9 load/save round trip.
    for (uint32_t value = 0; value <= 0xffff; ++value)
        assert(ThemeColorStorage::encode(ThemeColorStorage::decode(value)) == value);
    assert(ThemeColorStorage::encode(lv_color_hex(0xff0000)) == 0xf800);
    assert(ThemeColorStorage::encode(lv_color_hex(0x00ff00)) == 0x07e0);
    assert(ThemeColorStorage::encode(lv_color_hex(0x0000ff)) == 0x001f);

    lv_init();
    lv_log_register_print_cb(log_message);
    std::vector<uint16_t> first(800 * 480), second(first.size());
    lv_display_t *display = lv_display_create(800, 480);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, first.data(), second.data(), first.size() * 2,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);
    create_screens();
    assert(objects.label_co2_warmup);
    assert(lv_obj_has_flag(objects.label_co2_warmup, LV_OBJ_FLAG_HIDDEN));
    lv_obj_t *screens[] = {objects.page_boot_logo, objects.page_boot_diag,
        objects.page_main_pro, objects.page_settings, objects.page_wifi,
        objects.page_theme, objects.page_clock, objects.page_co2_calib,
        objects.page_auto_night_mode, objects.page_backlight, objects.page_mqtt,
        objects.page_sensors_info, objects.page_dac_settings,
        objects.page_fw_update, objects.page_diag, objects.page_main_pro,
        objects.page_main_pro, objects.page_main_pro, objects.page_sensors_info};
    for (size_t index = 0; index < sizeof(screens) / sizeof(*screens); ++index) {
        assert(screens[index]);
        if (index == 15) {
            const auto co2 = UiCo2CardPresentation::resolve(false, true);
            assert(co2.warmup && !co2.show_value && !co2.show_unit);
            lv_obj_remove_flag(objects.label_co2_warmup, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(objects.label_co2_value_1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(objects.label_co2_unit_1, LV_OBJ_FLAG_HIDDEN);
        }
        if (index == 16) {
            // Weather on (docs/WEATHER.md): the off layout must be exactly
            // the generated one, before and after a round trip.
            check_weather_off_geometry();
            UiWeatherLayout::apply(true, kFonts);
            UiWeatherLayout::apply(false, kFonts);
            check_weather_off_geometry();
            UiWeatherLayout::apply(true, kFonts);
            assert(!lv_obj_has_flag(objects.card_outdoor_pro, LV_OBJ_FLAG_HIDDEN));
            assert(lv_obj_has_flag(objects.chip_delta_25, LV_OBJ_FLAG_HIDDEN));
            // Five characters: the smaller digits, as the device picks them.
            lv_label_set_text(objects.label_outdoor_value, "-12.5");
            assert(!UiOutdoorCard::useLargeValueFont("-12.5"));
            lv_obj_set_style_text_font(objects.label_outdoor_value, &ui_font_jet_med_28, LV_PART_MAIN);
            lv_label_set_text(objects.label_pressure_title_1, "PRESSIONE MSL");  // longest title
            lv_label_set_text(objects.label_pressure_value_1, "1013");
            lv_label_set_text(objects.label_pressure_unit_1, "inHg");  // the wider unit
            check_inside_card(objects.card_pressure_pro);
            lv_label_set_text(objects.label_pressure_unit_1, "hPa");
            check_inside_card(objects.card_pressure_pro);
            check_inside_card(objects.card_outdoor_pro);
            check_single_line(objects.label_outdoor_value);
        }
        if (index == 17) {
            // A failed check: the reason, no value (longest English text).
            const auto s = UiOutdoorCard::resolve(WeatherApi::Status::ServiceError);
            assert(s.visible && !s.show_value);
            lv_obj_add_flag(objects.label_outdoor_value, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(objects.label_outdoor_unit, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(objects.label_outdoor_message, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(objects.label_outdoor_message, "Weather service error");
            check_inside_card(objects.card_outdoor_pro);
        }
        if (index == 18) {
            // The generated page shows pressure_info in its 3h text view.
            lv_obj_remove_flag(objects.label_pressure_info_24h, LV_OBJ_FLAG_HIDDEN);
        }
        lv_screen_load(screens[index]);
        lv_refr_now(display);
        assert(presented && flushed > 0);
        if (index == 18) {
            // Laid out by the render above: the 24h line stays on screen.
            lv_area_t label, box;
            lv_obj_get_coords(objects.label_pressure_info_24h, &label);
            lv_obj_get_coords(objects.pressure_info, &box);
            assert(label.x1 >= box.x1 && label.x2 <= box.x2 && label.y1 >= box.y1 && label.y2 <= box.y2);
        }
        auto path = std::filesystem::path(argv[1]) / (std::to_string(index) + ".ppm");
        FILE *out = std::fopen(path.string().c_str(), "wb");
        assert(out);
        std::fprintf(out, "P6\n800 480\n255\n");
        for (size_t pixel = 0; pixel < first.size(); ++pixel) {
            lv_color_t rgb = ThemeColorStorage::decode(presented[pixel]);
            const uint8_t data[] = {rgb.red, rgb.green, rgb.blue};
            assert(std::fwrite(data, 1, 3, out) == 3);
        }
        std::fclose(out);
    }
    lv_display_delete(display);
    lv_deinit();
    std::puts("All 15 generated screens, CO2 warmup and the weather-on layout rendered; all 65536 saved RGB565 colors round-trip.");
}
