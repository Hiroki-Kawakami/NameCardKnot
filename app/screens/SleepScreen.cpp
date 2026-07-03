/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "SleepScreen.hpp"
#include "NameCardKnot.hpp"
#include "HomeScreen.hpp"
#include "lv_image_adapter.hpp"
#include "widgets.hpp"
#include "resources.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

void SleepScreen::build() {
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(root_, 40, 0);
    lv_obj_set_style_pad_row(root_, 20, 0);

    lv_spacer_create(root_, 0, 0, 1);

    auto title = lv_label_create(root_);
    lv_label_set_text(title, "Name Card Knot");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);

    if (mycard::Store::available()) {
        preview_map_ = mycard::Store::map_image(mycard::BLOB_PREVIEW);
        if (l8view_fill_lv_dsc(preview_map_.view(), preview_dsc_)) {
            auto image = lv_image_create(root_);
            lv_image_set_src(image, &preview_dsc_);
            lv_obj_set_style_pad_top(image, 20, 0);
        }
        name_map_ = mycard::Store::map_image(mycard::BLOB_NAME);
        if (l8view_fill_lv_dsc(name_map_.view(), name_dsc_)) {
            auto name = lv_image_create(root_);
            lv_image_set_src(name, &name_dsc_);
            lv_obj_set_style_pad_top(name, 20, 0);
        }
    }

    lv_spacer_create(root_, 0, 0, 1);

    auto icon = lv_label_create(root_);
    lv_label_set_text(icon, LUCIDE_MOON);
    lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);

    auto hint = lv_label_create(root_);
#if defined(CONFIG_IDF_TARGET_ESP32)
    lv_label_set_text(hint, "Hold the side button to wake");
#else
    lv_label_set_text(hint, "Press the side button to wake");
#endif
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_24, 0);

    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_fn(root_, LV_EVENT_CLICKED, [](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
        screen_manager.load(std::make_shared<HomeScreen>());
    });
}
