/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ShareScreen.hpp"
#include "TransferScreen.hpp"
#include "NameCardKnot.hpp"
#include "Nvs.hpp"
#include "screen_manager.hpp"
#include "resources.h"
#include "Strings.hpp"
#include "UiFont.hpp"
#include <cstring>

void ShareScreen::build() {
    createNavigation(S().share);
    lv_obj_set_style_border_width(navigation_, 0, 0);
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(contents_, 20, 0);
    lv_obj_set_style_pad_row(contents_, 20, 0);
    lv_obj_set_style_pad_bottom(contents_, 20, 0);
    if (!data_ || !data_->valid()) return;

    if (!data_->name().empty()) {
        lv_obj_t *name = lv_label_create(contents_);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_text(name, data_->name().c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(name, nameFont(), 0);
        lv_obj_set_style_pad_bottom(name, 16, 0);
    }

    createShareImages(contents_);

    if (!data_->url().empty() || !data_->message().empty()) {
        auto row = lv_container_create(contents_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 10, 0);

        auto button = [row](const char *icon, const char *title, std::function<void(lv_event_t*)> clicked) {
            auto button = lv_button_create(row);
            lv_obj_remove_style_all(button);
            lv_obj_set_height(button, 64);
            lv_obj_set_flex_grow(button, 1);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(button, 10, 0);
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_radius(button, 8, 0);
            lv_obj_set_style_border_color(button, lv_color_white(), 0);
            lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);
            lv_obj_add_event_fn(button, LV_EVENT_CLICKED, clicked);

            auto icon_label = lv_label_create(button);
            lv_label_set_text(icon_label, icon);
            lv_obj_set_style_text_font(icon_label, R.font.lucide_40, 0);
            auto label = lv_label_create(button);
            lv_label_set_text(label, title);
            lv_obj_set_style_text_font(label, ui_font_32(), 0);
        };
        if (!data_->url().empty()) {
            button(LUCIDE_LINK, S().url, [this](lv_event_t*) {
                epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
                auto card = lv_modal_open(root_);
                lv_modal_title_create(card, S().url);
                lv_modal_message_create(card, data_->url().c_str());

                lv_modal_button_create(card, S().close, LV_MODAL_BUTTON_TYPE_PRIMARY, [card](lv_event_t *e) {
                    lv_async_call([card](){
                        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
                        lv_modal_close(card);
                    });
                });
            });
        }
        if (!data_->message().empty()) {
            if (lv_obj_get_child_count(row) > 0) lv_ver_separator_create(row);
            button(LUCIDE_MESSAGE_CIRCLE, S().message, [this](lv_event_t*) {
                epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
                auto card = lv_modal_open(root_);
                lv_modal_title_create(card, S().message);
                lv_modal_message_create(card, data_->message().c_str());

                lv_modal_button_create(card, S().close, LV_MODAL_BUTTON_TYPE_PRIMARY, [card](lv_event_t *e) {
                    lv_async_call([card](){
                        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
                        lv_modal_close(card);
                    });
                });
            });
        }
    }

    createHotKnotStartMessage(contents_);

    if (!mount_sd_card()) {  // receiving a card in return needs the SD card
        auto label = lv_label_create(contents_);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_text(label, S().sd_required_to_receive);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, ui_font_24(), 0);
    } else {
        auto button = lv_button_create(contents_);
        lv_obj_set_size(button, LV_PCT(100), 64);

        auto container = lv_container_create(button, LV_FLEX_FLOW_ROW);
        lv_obj_center(container);
        lv_obj_set_size(container, 500, 64);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(container, 20, 0);
        lv_obj_set_style_pad_hor(container, 20, 0);
        lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);

        allow_return_data_ = settings::share_receive_return();
        auto createCheckbox = [this, container](lv_event_t *e) {
            if (e) settings::set_share_receive_return(allow_return_data_ = !allow_return_data_);

            lv_obj_clean(container);
            auto checkbox = lv_label_create(container);
            lv_obj_set_style_text_font(checkbox, ui_font_24(), 0);
            lv_obj_set_size(checkbox, 24, 24);
            lv_obj_set_style_border_width(checkbox, 1, 0);
            if (allow_return_data_) {
                lv_label_set_text(checkbox, LV_SYMBOL_OK);
                lv_obj_set_style_border_color(checkbox, lv_color_white(), 0);
            } else {
                lv_label_set_text(checkbox, "");
                lv_obj_set_style_border_color(checkbox, lv_color_black(), 0);
            }

            auto label = lv_label_create(container);
            lv_label_set_text(label, S().also_receive_return);
            lv_obj_set_style_text_font(label, ui_font_24(), 0);
        };
        createCheckbox(nullptr);
        lv_obj_add_event_fn(button, LV_EVENT_CLICKED, createCheckbox);
    }

    startHotKnot(BSP_HOTKNOT_ROLE_MASTER);
}

void ShareScreen::onHotKnotReady() {
    if (dokan_descriptor_create(DOKAN_TRANSPORT_WIFI, DOKAN_APP_ID,
                                descriptor_, sizeof descriptor_) != ESP_OK) {
        failHotKnot(ESP_FAIL);
        return;
    }
    setSessionMessage("Sending...");
    // HotKnot frames are even-length; send the descriptor including its NUL.
    size_t len = strlen(descriptor_) + 1;
    if (len & 1) len++;
    sendHotKnot(descriptor_, len);
}

void ShareScreen::onHotKnotDone() {
    endHotKnot();
    TransferStart start{};
    start.role = DOKAN_ROLE_HOST;       // master establishes the SoftAP
    start.offer = data_ && data_->valid();
    start.accept = allow_return_data_;
    memcpy(start.descriptor, descriptor_, sizeof start.descriptor);
    start.own = data_;
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
    screen_manager.load(std::make_shared<TransferScreen>(std::move(start)));
}
