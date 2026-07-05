/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "AcknowledgementsScreen.hpp"
#include "AcknowledgementsText.hpp"
#include "NameCardKnot.hpp"
#include "Strings.hpp"
#include "UiFont.hpp"
#include "resources.h"
#include <cstdio>

static lv_obj_t *page_button_create(lv_obj_t *parent, const char *icon, lv_align_t align,
                                    std::function<void(lv_event_t*)> on_click) {
    auto button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 60, 60);
    lv_obj_align(button, align, align == LV_ALIGN_LEFT_MID ? 12 : -12, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_border_color(button, lv_color_white(), 0);
    lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_add_event_fn(button, LV_EVENT_CLICKED, on_click);

    auto label = lv_label_create(button);
    lv_obj_center(label);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, R.font.lucide_40, 0);
    return button;
}

void AcknowledgementsScreen::build() {
    createNavigation(S().acknowledgements);

    viewport_ = lv_container_create(contents_, lv_color_white());
    lv_obj_set_width(viewport_, LV_PCT(100));
    lv_obj_set_flex_grow(viewport_, 1);
    lv_obj_set_style_pad_all(viewport_, 20, 0);
    lv_obj_set_scroll_dir(viewport_, LV_DIR_NONE);  // paged by buttons, not touch-drag

    auto text = lv_label_create(viewport_);
    lv_label_set_text_static(text, kAcknowledgementsText);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, LV_PCT(100));
    lv_obj_set_style_text_font(text, &lv_font_montserrat_14, 0);

    auto status = lv_container_create(contents_, lv_color_white());
    lv_obj_set_size(status, LV_PCT(100), 81);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(status, 2, 0);
    lv_obj_set_style_border_color(status, lv_color_black(), 0);

    page_label_ = lv_label_create(status);
    lv_obj_set_style_text_font(page_label_, ui_font_24(), 0);
    lv_obj_center(page_label_);

    prev_button_ = page_button_create(status, LUCIDE_CIRCLE_ARROW_LEFT, LV_ALIGN_LEFT_MID,
                                      [this](lv_event_t*) { showPage(page_ - 1); });
    next_button_ = page_button_create(status, LUCIDE_CIRCLE_ARROW_RIGHT, LV_ALIGN_RIGHT_MID,
                                      [this](lv_event_t*) { showPage(page_ + 1); });

    lv_obj_update_layout(root_);
    page_height_ = lv_obj_get_content_height(viewport_);
    scroll_max_ = lv_obj_get_scroll_bottom(viewport_);
    page_count_ = page_height_ > 0 ? 1 + (scroll_max_ + page_height_ - 1) / page_height_ : 1;

    showPage(0);
}

void AcknowledgementsScreen::showPage(int page) {
    if (page < 0) page = 0;
    if (page >= page_count_) page = page_count_ - 1;
    page_ = page;

    int y = page * page_height_;
    if (y > scroll_max_) y = scroll_max_;
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
    lv_obj_scroll_to_y(viewport_, y, LV_ANIM_OFF);

    char buf[32];
    snprintf(buf, sizeof buf, "%d / %d", page_ + 1, page_count_);
    lv_label_set_text(page_label_, buf);

    if (page_ > 0) lv_obj_remove_flag(prev_button_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(prev_button_, LV_OBJ_FLAG_HIDDEN);
    if (page_ + 1 < page_count_) lv_obj_remove_flag(next_button_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(next_button_, LV_OBJ_FLAG_HIDDEN);
}
