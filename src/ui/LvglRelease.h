// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include <lvgl.h>

// Deletes objects the controller made at runtime and forgets them, shared by
// the sensor charts and the multi-sensor chart.
inline void release_lvgl_obj(lv_obj_t *&obj) {
    if (obj && lv_obj_is_valid(obj)) {
        lv_obj_del(obj);
    }
    obj = nullptr;
}

inline void release_lvgl_obj_array(lv_obj_t **objs, uint8_t count) {
    if (!objs) {
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        release_lvgl_obj(objs[i]);
    }
}
