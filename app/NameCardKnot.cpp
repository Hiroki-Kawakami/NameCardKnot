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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <assert.h>
#include "HomeScreen.hpp"
#include "NameCardScreen.hpp"
#include "CardStore.hpp"
#include "LastCard.hpp"
#include "Power.hpp"

static const char *TAG = "NameCardKnot";

// Touch cache fed by the BSP push callback (off the LVGL thread, hence the lock).
// `armed` latches a press edge so a tap landing entirely between two LVGL reads
// (e.g. during an EPD refresh) still clicks. See docs/gotchas.md.
static struct {
    SemaphoreHandle_t lock;
    int  x, y;
    bool down;
    bool armed;
} s_touch;

static void touch_event_cb(const bsp_touch_point_t *pts, int count, void *) {
    xSemaphoreTake(s_touch.lock, portMAX_DELAY);
    if (count > 0) {
        if (!s_touch.down) s_touch.armed = true;
        s_touch.down = true;
        s_touch.x = pts[0].x;
        s_touch.y = pts[0].y;
    } else {
        s_touch.down = false;
    }
    xSemaphoreGive(s_touch.lock);
}

static lv_display_t *s_disp;
static uint8_t *s_buf;
static bool s_dirty_valid;
static int s_dx1, s_dy1, s_dx2, s_dy2;
static bsp_epd_mode_t s_default_mode, s_next_mode;
static bool s_next_valid;   // lets next = NONE mean "this flush refreshes nothing"

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
    s_next_valid = true;
}

static void lvgl_init() {
    lvgl_port_cfg_t config = {
        // LVGL shares core 0 with the EPD refresh task (priority 5 > LVGL 4, so a
        // refresh preempts the UI); the async image decoder owns core 1.
        .task_priority = 4,
        .task_stack = 16384,
        .task_affinity = 0,
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
            bsp_epd_mode_t mode = s_next_valid ? s_next_mode : s_default_mode;
            s_next_valid = false;
            if (mode != BSP_EPD_MODE_NONE) {
                dirty_refresh(mode);
            } else {
                s_dirty_valid = false;   // suppressed flush: GRAM only, drop the rect
            }
        }
        lv_display_flush_ready(disp);
    });
    lv_display_set_default(s_disp);

    s_touch.lock = xSemaphoreCreateMutex();
    bsp_touch_set_event_cb(touch_event_cb, nullptr);

    lv_group_set_default(lv_group_create());
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, [](lv_indev_t*, lv_indev_data_t *data) {
        xSemaphoreTake(s_touch.lock, portMAX_DELAY);
        if (s_touch.down || s_touch.armed) {
            data->point.x = s_touch.x;
            data->point.y = s_touch.y;
            data->state   = LV_INDEV_STATE_PRESSED;
            s_touch.armed = false;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
        xSemaphoreGive(s_touch.lock);
    });
    lv_indev_set_display(indev, s_disp);
    lv_indev_set_group(indev, lv_group_get_default());

    // EPD: draws only update GRAM until a refresh
    bsp_display_set_epd_mode(BSP_EPD_MODE_NONE);
}

// The decode options every display path uses (the logical portrait resolution).
static imgproc::Options display_opts() {
    const bsp_size_t size = bsp_display_get_size();   // panel-native landscape
    imgproc::Options opts;
    opts.target_w = size.height;
    opts.target_h = size.width;
    opts.fit = imgproc::Fit::Contain;
    opts.levels = 16;
    return opts;
}

// Boot resume: the card recorded in NVS, restored without the SD card or a
// decode when possible (mycard / lastcard mmap). nullptr -> not restorable
// instantly (caller falls back to an SD load or Home).
static std::shared_ptr<NameCardData> resume_data(const lastcard::Info &info) {
    if (info.source == lastcard::Source::MyCard) {
        auto data = NameCardData::load_cached();
        if (!data) lastcard::clear();
        return data;
    }
    if (info.source == lastcard::Source::SdFile && lastcard::cache_path() == info.path)
        return NameCardData::load_lastcard(info.path, display_opts());
    return nullptr;
}

bool mount_sd_card() {
    if (!bsp_sd_is_mounted()) {
        esp_err_t err = bsp_sd_mount("/sdcard", NULL);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return false;
        }
    }
    return true;
}

void unmount_sd_card() {
    bsp_sd_unmount();
}

void app_entry() {
    bsp_config_t bsp_config = {};
    bsp_config.epd.task_priority = 5;
    bsp_config.epd.task_affinity = 1;
    bsp_config.touch.task_priority = 6;
    bsp_config.touch.task_affinity = 1;
    bsp_init(&bsp_config);
    cardstore::mycard().mount();

    bsp_rtc_timer_stop();
    lvgl_init();

    epd_set_default_refresh_mode(BSP_EPD_MODE_FAST);   // ongoing updates: diff
    lv_async_call([](void*) {
        const auto info = lastcard::load();
        auto data = resume_data(info);
        std::shared_ptr<NameCardScreen> card;
        if (data) {
            card = std::make_shared<NameCardScreen>(data, NameCardScreen::Nav::Home);
        } else if (info.source == lastcard::Source::SdFile && mount_sd_card()) {
            card = std::make_shared<NameCardScreen>(NameCardData::load(info.path, display_opts()),
                                                    NameCardScreen::Nav::Home);
        }
        if (card) {
            card->set_clean_resume(lastcard::clean_resume());
            screen_manager.load(card);
            if (lastcard::clean_resume()) {
                lv_refr_now(NULL);   // paint-nothing first flush, then just the menu
                card->refreshMenu();
            }
        } else {
            screen_manager.load(std::make_shared<HomeScreen>());
        }
        power::start();
    }, nullptr);
}
