/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ReceiveScreen.hpp"
#include "TransferScreen.hpp"
#include "NameCardKnot.hpp"
#include "Nvs.hpp"
#include "screen_manager.hpp"
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

    if (!mount_sd_card()) {  // received cards are written to the SD card
        showProgressModal("An SD card is required to receive cards.");
        addModalCloseButton();
        return;
    }

    createHotKnotStartMessage(contents_);
    if (data_) loadShareCardData();

    startHotKnot(BSP_HOTKNOT_ROLE_SLAVE);
}

void ReceiveScreen::stashReceived(const uint8_t *data, size_t len) {
    size_t n = len < sizeof descriptor_ ? len : sizeof descriptor_ - 1;
    memcpy(descriptor_, data, n);
    descriptor_[n] = '\0';
}

void ReceiveScreen::onHotKnotDone() {
    if (!dokan_descriptor_valid(descriptor_, DOKAN_APP_ID)) {
        failAndReboot("Received an invalid descriptor.");
        return;
    }
    endHotKnot();
    TransferStart start{};
    start.role = DOKAN_ROLE_CLIENT;     // slave joins the SoftAP
    start.offer = return_my_data_ && data_ && data_->valid();
    start.accept = true;
    memcpy(start.descriptor, descriptor_, sizeof start.descriptor);
    start.own = data_;
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
    screen_manager.load(std::make_shared<TransferScreen>(std::move(start)));
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

        return_my_data_ = settings::receive_send_return();
        auto createCheckbox = [this, container](lv_event_t *e) {
            if (e) settings::set_receive_send_return(return_my_data_ = !return_my_data_);

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
