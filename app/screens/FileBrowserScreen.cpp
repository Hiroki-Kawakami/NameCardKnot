/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "FileBrowserScreen.hpp"
#include <string>

void FileBrowserScreen::build() {
    createNavigation("File Browser");
    rebuild();
}

void FileBrowserScreen::rebuild() {
    lv_obj_clean(contents_);

    for (int i = 0; i < 10; i++) {
        if (i) lv_hor_separator_create(contents_);

        auto row = lv_button_create(contents_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 77);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_color(row, lv_color_black(), LV_STATE_PRESSED);

        auto icon = lv_label_create(row);
        lv_obj_set_width(icon, 48);
        lv_label_set_text(icon, LV_SYMBOL_FILE);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        auto label = lv_label_create(row);
        lv_label_set_text(label, ("File" + std::to_string(i)).c_str());
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    }

    auto status = lv_container_create(contents_);
    lv_obj_set_size(status, LV_PCT(100), 81);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(status, 2, 0);
    lv_obj_set_style_border_color(status, lv_color_black(), 0);
}
