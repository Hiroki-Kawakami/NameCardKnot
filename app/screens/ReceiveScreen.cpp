/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ReceiveScreen.hpp"
#include "NameCardKnot.hpp"
#include "resources.h"
#include "dokan.h"
#include <cstring>

void ReceiveScreen::build() {
    createNavigation("Receive");
    lv_obj_set_style_border_width(navigation_, 0, 0);
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(contents_, 20, 0);
    lv_obj_set_style_pad_row(contents_, 20, 0);
    lv_obj_set_style_pad_bottom(contents_, 20, 0);

    {
        auto container = lv_container_create(contents_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(container, 1, 0);
        lv_obj_set_style_border_color(container, lv_color_black(), 0);
        lv_obj_set_style_border_side(container, (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
        lv_obj_set_flex_grow(container, 1);
        lv_obj_set_width(container, LV_PCT(100));
        lv_obj_set_style_pad_row(container, 20, 0);

        auto icon = lv_label_create(container);
        lv_label_set_text(icon, LUCIDE_SMARTPHONE_NFC);
        lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);

        auto msg = lv_label_create(container);
        lv_label_set_text(msg, "Hold this screen against the other device's screen to start sharing.");
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_24, 0);
    }

    if (data_) {
        loadShareCardData();
    }

    startHotKnot(BSP_HOTKNOT_ROLE_SLAVE);
}

void ReceiveScreen::stashReceived(const uint8_t *data, size_t len) {
    size_t n = len < sizeof descriptor_ ? len : sizeof descriptor_ - 1;
    memcpy(descriptor_, data, n);
    descriptor_[n] = '\0';
}

void ReceiveScreen::onHotKnotDone() {
    endHotKnot();
    if (dokan_descriptor_valid(descriptor_, DOKAN_APP_ID)) setProgressMessage(descriptor_);
    else setProgressMessage("Received an invalid descriptor.");
    addModalCloseButton();
}

void ReceiveScreen::loadShareCardData() {
    if (!data_ || !data_->valid()) return;

    auto button = lv_button_create(contents_);
    lv_obj_remove_style_all(button);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_width(button, 2, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(button, 1, 0);
    lv_obj_set_style_pad_all(button, 0, LV_STATE_PRESSED);
    lv_obj_set_style_flex_flow(button, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    {
        auto container = lv_container_create(button, LV_FLEX_FLOW_ROW);
        lv_obj_center(container);
        lv_obj_set_size(container, 500, 64);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(container, 20, 0);
        lv_obj_set_style_pad_hor(container, 20, 0);
        lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);

        auto createCheckbox = [this, container](lv_event_t *e) {
            if (e) return_my_data_ = !return_my_data_;

            lv_obj_clean(container);
            auto checkbox = lv_label_create(container);
            lv_obj_set_style_text_font(checkbox, &lv_font_montserrat_24, 0);
            lv_obj_set_size(checkbox, 24, 24);
            lv_obj_set_style_border_width(checkbox, 1, 0);
            if (return_my_data_) {
                lv_label_set_text(checkbox, LV_SYMBOL_OK);
                lv_obj_set_style_border_color(checkbox, lv_color_white(), 0);
            } else {
                lv_label_set_text(checkbox, "");
                lv_obj_set_style_border_color(checkbox, lv_color_black(), 0);
            }

            auto label = lv_label_create(container);
            lv_label_set_text(label, "Also send my card in return");
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        };
        createCheckbox(nullptr);
        lv_obj_add_event_fn(button, LV_EVENT_CLICKED, createCheckbox);
    }

    if (!data_->name().empty()) {
        lv_obj_t *name = lv_label_create(button);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_text(name, data_->name().c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(name, nameFont(), 0);
        lv_obj_set_style_pad_hor(name, 20, 0);
        lv_obj_set_style_pad_bottom(name, 16, 0);
    }

    createShareImages(button);
}
