/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "GrayscaleTestScreen.hpp"
#include "widgets.hpp"
#include "NameCardKnot.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void GrayscaleTestScreen::build() {
    bsp_display_refresh({}, BSP_EPD_MODE_CLEAR_FULL);
    vTaskDelay(pdMS_TO_TICKS(2000));
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);

    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root_, 0, 0);

    for (int i = 0; i < 16; i++) {
        int l8 = i * 17;
        auto strip = lv_container_create(root_, lv_color_make(l8, l8, l8));
        lv_obj_set_size(strip, LV_PCT(100), 60);
        lv_obj_remove_flag(strip, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_fn(root_, LV_EVENT_CLICKED, [](lv_event_t*) {
        bsp_display_refresh({}, BSP_EPD_MODE_QUALITY_FULL);
    });
}
