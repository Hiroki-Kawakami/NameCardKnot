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
#include "Strings.hpp"
#include "UiFont.hpp"
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
    lv_label_set_text(title, S().app_name);
    lv_obj_set_style_text_font(title, ui_font_48(), 0);

    if (cardstore::mycard().available()) {
        if (readPreview()) {
            auto image = lv_image_create(root_);
            lv_image_set_src(image, &preview_dsc_);
            lv_obj_set_style_pad_top(image, 20, 0);
        }
        if (name_.load_mycard(32))
            lv_obj_set_style_pad_top(name_.make_label(root_), 20, 0);
    }

    lv_spacer_create(root_, 0, 0, 1);

    auto icon = lv_label_create(root_);
    lv_label_set_text(icon, LUCIDE_MOON);
    lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);

    auto hint = lv_label_create(root_);
#if defined(CONFIG_IDF_TARGET_ESP32)
    lv_label_set_text(hint, S().hold_button_wake);
#else
    lv_label_set_text(hint, S().press_button_wake);
#endif
    lv_obj_set_style_text_font(hint, ui_font_24(), 0);

    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_fn(root_, LV_EVENT_CLICKED, [](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
        screen_manager.load(std::make_shared<HomeScreen>());
    });
}

bool SleepScreen::readPreview() {
    auto &st = cardstore::mycard();
    const cardstore::Header *h = st.header();
    if (!h) return false;
    const cardstore::Blob &b = h->blobs[cardstore::BLOB_PREVIEW];
    if (!b.length) return false;
    preview_buf_.resize(b.length);
    if (!st.read_blob(cardstore::BLOB_PREVIEW, preview_buf_.data(), b.length)) return false;
    return l8view_fill_lv_dsc(L8View{preview_buf_.data(), b.w, b.h, b.stride, b.levels}, preview_dsc_);
}
