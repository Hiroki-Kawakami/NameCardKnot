/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "NameCardScreen.hpp"
#include "NameCardKnot.hpp"
#include "lv_image_adapter.hpp"
#include "widgets.hpp"
#include "resources.h"

NameCardScreen::NameCardScreen(std::shared_ptr<NameCardData> data) : data_(std::move(data)) {}

void NameCardScreen::build() {
    // The image is already decoded (NameCardData, at the display resolution), so
    // just show it 1:1 (no LVGL scaling) via the L8 -> lv_image_dsc adapter.
    if (imgproc_fill_lv_dsc(data_->display_image(), dsc_)) {
        auto image = lv_image_create(root_);
        lv_image_set_src(image, &dsc_);
        lv_obj_center(image);
    } else {
        auto label = lv_label_create(root_);
        lv_label_set_text(label, "No image");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_center(label);
    }

    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_fn(root_, LV_EVENT_CLICKED, [this](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
        openMenu();
    });
}

void NameCardScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);
}

void NameCardScreen::openMenu() {
    auto card = lv_modal_open(root_);
    lv_obj_set_style_pad_row(card, 10, 0);

    { // Buttons
        auto button = [](lv_obj_t *parent, const char *icon, const char *title, std::function<void(lv_event_t*)> on_click) {
            auto button = lv_button_create(parent);
            lv_obj_set_height(button, 100);
            lv_obj_set_flex_grow(button, 1);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_radius(button, 8, 0);
            lv_obj_set_style_border_color(button, lv_color_white(), 0);
            lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
            lv_obj_add_event_fn(button, LV_EVENT_CLICKED, on_click);

            auto image = lv_label_create(button);
            lv_label_set_text(image, icon);
            lv_obj_set_style_text_font(image, R.font.lucide_40, 0);

            auto label = lv_label_create(button);
            lv_label_set_text(label, title);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        };

        auto row1 = lv_container_create(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(row1, 10, 0);
        lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row1, LUCIDE_SQUARE_ARROW_RIGHT_ENTER, "Receive", [](lv_event_t*) {});
        lv_ver_separator_create(row1);
        button(row1, LUCIDE_SQUARE_ARROW_OUT_UP_RIGHT, "Share", [](lv_event_t*) {});

        lv_hor_separator_create(card);

        auto row2 = lv_container_create(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(row2, 10, 0);
        lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row2, LUCIDE_HOME, "Home", [](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);
            screen_manager.pop();
        });
        lv_ver_separator_create(row2);
        button(row2, LUCIDE_COG, "Settings", [](lv_event_t*) {});
        lv_ver_separator_create(row2);
        button(row2, LUCIDE_X, "Close", [card](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);
            lv_modal_close(card);
        });
    }
}
