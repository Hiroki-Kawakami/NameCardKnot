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
    // The image is already at display resolution (decoded for an SD load, or
    // mapped from flash for a cached card), so show it 1:1 (no LVGL scaling).
    if (l8view_fill_lv_dsc(data_->display_view(), dsc_)) {
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
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
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

        if (data_->is_card()) {
            auto row1 = lv_container_create(card, LV_FLEX_FLOW_ROW);
            lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_column(row1, 10, 0);
            lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            button(row1, LUCIDE_SQUARE_ARROW_RIGHT_ENTER, "Receive", [](lv_event_t*) {});
            lv_ver_separator_create(row1);
            button(row1, LUCIDE_SQUARE_ARROW_OUT_UP_RIGHT, "Share", [](lv_event_t*) {});
            lv_ver_separator_create(row1);
            button(row1, LUCIDE_INFO, "Info", [this, card](lv_event_t*) {
                epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
                lv_async_call([this, card]() {
                    lv_modal_close(card);
                    openInfo();
                });
            });

            lv_hor_separator_create(card);
        }

        auto row2 = lv_container_create(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(row2, 10, 0);
        lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row2, LUCIDE_HOME, "Home", [](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            screen_manager.pop();
        });
        lv_ver_separator_create(row2);
        button(row2, LUCIDE_COG, "Settings", [](lv_event_t*) {});
        lv_ver_separator_create(row2);
        button(row2, LUCIDE_X, "Close", [card](lv_event_t*) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
            lv_modal_close(card);
        });
    }
}

const lv_font_t *NameCardScreen::nameFont() {
    if (!name_font_) name_font_ = std::make_unique<NameFont>(data_->name_glyphs());
    return name_font_->font();
}

void NameCardScreen::openInfo() {
    auto card = lv_modal_open(root_);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, data_->name().c_str());
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_font(name, nameFont(), 0);
    lv_obj_set_style_pad_ver(name, 10, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *qr = lv_qrcode_create(card);
    lv_obj_set_width(qr, LV_PCT(100));
    lv_qrcode_set_size(qr, 300);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, data_->url().c_str(), data_->url().size());

    lv_obj_t *url = lv_label_create(card);
    lv_label_set_text(url, data_->url().c_str());
    lv_obj_set_width(url, LV_PCT(100));
    lv_obj_set_style_pad_ver(url, 10, 0);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);

    lv_modal_button_create(card, "Close", LV_MODAL_BUTTON_TYPE_PRIMARY, [card](lv_event_t*) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
        lv_modal_close(card);
    });
}
