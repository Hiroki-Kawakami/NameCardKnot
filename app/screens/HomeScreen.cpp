/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "HomeScreen.hpp"
#include "widgets.hpp"
#include "resources.h"
#include "NameCardKnot.hpp"
#include "FileBrowserScreen.hpp"
#include "GrayscaleTestScreen.hpp"

void HomeScreen::build() {
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(root_, 20, 0);

    { // Title
        lv_obj_t *title = lv_label_create(root_);
        lv_label_set_text(title, "Name Card Knot");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);

        lv_obj_t *version = lv_label_create(root_);
        lv_label_set_text(version, "v0.1.0");
        lv_obj_set_style_text_font(version, &lv_font_montserrat_24, 0);

        lv_obj_set_style_pad_top(title, 40, 0);
        lv_obj_set_style_pad_bottom(version, 40, 0);
    }

    { // My Card
        auto button = lv_button_create(root_);
        lv_obj_set_size(button, LV_PCT(100), 320);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto image = lv_image_create(button);
        lv_obj_set_size(image, 169, 300);
        lv_obj_set_style_bg_color(image, lv_color_hex(0x888888), 0);
        lv_obj_set_style_bg_opa(image, LV_OPA_COVER, 0);

        auto container = lv_container_create(button, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(container, 304, 300);
        lv_obj_set_style_pad_row(container, 10, 0);

        auto title = lv_label_create(container);
        lv_label_set_text(title, "My Card");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
        lv_obj_set_style_pad_top(title, 20, 0);
        lv_spacer_create(container, 0, 0, 1);

        auto name = lv_label_create(container);
        lv_label_set_text(name, "Hiroki Kawakami");
        lv_obj_set_style_text_font(name, &lv_font_montserrat_32, 0);

        auto id = lv_label_create(container);
        lv_label_set_text(id, "@hiroki_cockatoo");
        lv_obj_set_style_text_font(id, &lv_font_montserrat_24, 0);

        lv_spacer_create(container, 0, 0, 1);

        lv_hor_separator_create(container, 20);
        auto share_button = lv_button_create(container);
        lv_obj_remove_style_all(share_button);
        lv_obj_set_size(share_button, LV_PCT(100), 80);
        lv_obj_set_style_margin_hor(share_button, 20, 0);
        lv_obj_set_flex_flow(share_button, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(share_button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(share_button, 10, 0);
        lv_obj_set_style_border_width(share_button, 2, 0);
        lv_obj_set_style_radius(share_button, 8, 0);
        lv_obj_set_style_border_color(share_button, lv_color_white(), 0);
        lv_obj_set_style_border_color(share_button, lv_color_black(), LV_STATE_PRESSED);

        auto share_icon = lv_label_create(share_button);
        lv_label_set_text(share_icon, LUCIDE_SQUARE_ARROW_OUT_UP_RIGHT);
        lv_obj_set_style_text_font(share_icon, R.font.lucide_40, 0);
        auto share_label = lv_label_create(share_button);
        lv_label_set_text(share_label, "Share");
        lv_obj_set_style_text_font(share_label, &lv_font_montserrat_32, 0);

    }

    { // Buttons
        auto button = [](lv_obj_t *parent, const void *icon, const char *title, std::function<void(lv_event_t*)> on_click) {
            auto button = lv_button_create(parent);
            lv_obj_set_size(button, 200, 140);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_radius(button, 8, 0);
            lv_obj_set_style_border_color(button, lv_color_white(), 0);
            lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
            lv_obj_add_event_fn(button, LV_EVENT_CLICKED, on_click);

            auto image = lv_image_create(button);
            lv_image_set_src(image, icon);
            lv_obj_set_style_image_recolor(image, lv_color_black(), 0);
            lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);

            auto label = lv_label_create(button);
            lv_label_set_text(label, title);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        };

        auto row1 = lv_container_create(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row1, 10, 0);
        lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row1, R.icon.card_sd_80px, "Open from SD", [](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);
            screen_manager.push(std::make_shared<FileBrowserScreen>());
        });
        lv_ver_separator_create(row1);
        button(row1, R.icon.images_80px, "Gallery", [](lv_event_t*) {});

        lv_hor_separator_create(root_, 20);

        auto row2 = lv_container_create(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row2, 10, 0);
        lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row2, R.icon.square_arrow_right_enter_80px, "Receive", [this](lv_event_t*) {
            lv_modal_open(root_);
        });
        lv_ver_separator_create(row2);
        button(row2, R.icon.cog_80px, "Settings", [](lv_event_t*) {
            screen_manager.push(std::make_shared<GrayscaleTestScreen>());
        });
    }
}

void HomeScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);
}
