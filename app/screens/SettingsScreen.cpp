/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "SettingsScreen.hpp"
#include "DateTimeScreen.hpp"
#include "GrayscaleTestScreen.hpp"
#include "NameCardKnot.hpp"

static lv_obj_t *row_create(lv_obj_t *parent, const char *title, std::function<void(lv_event_t*)> on_click) {
    auto row = lv_button_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 77);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, lv_color_white(), 0);
    lv_obj_set_style_border_color(row, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_add_event_fn(row, LV_EVENT_CLICKED, on_click);

    auto label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    return row;
}

void SettingsScreen::build() {
    createNavigation("Settings");

    row_create(contents_, "Date & Time", [](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
        screen_manager.push(std::make_shared<DateTimeScreen>(DateTimeScreen::Nav::Back));
    });
    lv_hor_separator_create(contents_);
    row_create(contents_, "Grayscale Test", [](lv_event_t*) {
        screen_manager.push(std::make_shared<GrayscaleTestScreen>());
    });
}
