/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "NameCardKnot.hpp"
#include "bsp.h"
#include "lvgl.hpp"
#include "screen_manager.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <assert.h>
#include "HomeScreen.hpp"

static const char *TAG = "NameCardKnot";

static lv_display_t *s_disp;
static uint8_t *s_buf;
static bool s_dirty_valid;
static int s_dx1, s_dy1, s_dx2, s_dy2;
static bsp_epd_mode_t s_default_mode, s_next_mode;

static void dirty_extend(int x1, int y1, int x2, int y2) {
    if (!s_dirty_valid) {
        s_dx1 = x1; s_dy1 = y1; s_dx2 = x2; s_dy2 = y2;
        s_dirty_valid = true;
        return;
    }
    if (x1 < s_dx1) s_dx1 = x1;
    if (y1 < s_dy1) s_dy1 = y1;
    if (x2 > s_dx2) s_dx2 = x2;
    if (y2 > s_dy2) s_dy2 = y2;
}

static void dirty_refresh(bsp_epd_mode_t mode) {
    bsp_rect_t rect = s_dirty_valid
        ? (bsp_rect_t){ { s_dx1, s_dy1 }, { s_dx2 - s_dx1 + 1, s_dy2 - s_dy1 + 1 } }
        : (bsp_rect_t){ { 0, 0 }, bsp_display_get_size() };
    bsp_display_refresh(rect, mode);
    s_dirty_valid = false;
}

void epd_set_default_refresh_mode(bsp_epd_mode_t mode) {
    s_default_mode = mode;
}

void epd_set_next_refresh_mode(bsp_epd_mode_t mode) {
    s_next_mode = mode;
}

static void lvgl_init() {
    lvgl_port_cfg_t config = {
        .task_priority = 4,
        .task_stack = 7168,
        .task_affinity = 1,
        .task_max_sleep_ms = 500,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
        .timer_period_ms = 5,
    };
    esp_err_t err = lvgl_port_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL: %s", esp_err_to_name(err));
        assert(0);
    }

    const bsp_size_t         size = bsp_display_get_size();
    const bsp_pixel_format_t fmt  = bsp_display_get_pixel_format();
    const size_t buf_bytes = (size_t)size.width * size.height * bsp_pixel_format_bytes(fmt) / 4;

    s_buf = (uint8_t *)heap_caps_aligned_alloc(4, buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_disp = lv_display_create(size.width, size.height);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_L8);
    lv_display_set_buffers(s_disp, s_buf, NULL, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_90);
    lv_display_set_flush_cb(s_disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
        lv_area_t rot = *area;
        lv_display_rotate_area(disp, &rot);   // logical area -> physical panel coords
        bsp_rect_t rect = { { rot.x1, rot.y1 }, { rot.x2 - rot.x1 + 1, rot.y2 - rot.y1 + 1 } };
        bsp_display_draw_bitmap(rect, px_map, BSP_ROTATION_90);
        dirty_extend(rot.x1, rot.y1, rot.x2, rot.y2);

        if (lv_display_flush_is_last(disp)) {
            bsp_epd_mode_t mode = s_next_mode ? s_next_mode : s_default_mode;
            s_next_mode = BSP_EPD_MODE_NONE;
            if (mode != BSP_EPD_MODE_NONE) dirty_refresh(mode);
        }
        lv_display_flush_ready(disp);
    });
    lv_display_set_default(s_disp);

    lv_group_set_default(lv_group_create());
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, [](lv_indev_t*, lv_indev_data_t *data) {
        bsp_touch_point_t pt;
        if (bsp_touch_read(&pt, 1) > 0) {
            data->point.x = pt.x;
            data->point.y = pt.y;
            data->state   = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    });
    lv_indev_set_display(indev, s_disp);
    lv_indev_set_group(indev, lv_group_get_default());

    // EPD: draws only update GRAM until a refresh
    bsp_display_set_epd_mode(BSP_EPD_MODE_NONE);
}

void app_entry() {
    bsp_init(nullptr);
    lvgl_init();

    epd_set_default_refresh_mode(BSP_EPD_MODE_FAST);   // ongoing updates: diff
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL); // first paint: full flush
    lv_async_call([](){
        screen_manager.load(std::make_shared<HomeScreen>());
    });
}
