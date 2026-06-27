/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ShareScreen.hpp"
#include <cstdlib>
#include <string>
#include "bsp.h"

void ShareScreen::build() {
    createNavigation("HotKnot Send");
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    value_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(value_label_, &lv_font_montserrat_48, 0);
    lv_label_set_text(value_label_, "----");

    status_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_24, 0);
    lv_label_set_text(status_label_, "");
}

void ShareScreen::onAppear() {
    sent_ = false;
    value_ = rand() % 10000;
    lv_label_set_text_fmt(value_label_, "%d", value_);
    lv_label_set_text(status_label_, "Approaching...");

    bsp_hotknot_begin(BSP_HOTKNOT_ROLE_MASTER, [](const bsp_hotknot_event_t *ev, void *arg) {
        auto self = (ShareScreen*)arg;
        switch (ev->type) {
        case BSP_HOTKNOT_EVENT_PAIRED:
            lv_async_call([self]{ lv_label_set_text(self->status_label_, "Paired..."); });
            break;
        case BSP_HOTKNOT_EVENT_READY:
            lv_async_call([self]{
                if (self->sent_) return;
                self->sent_ = true;
                auto msg = std::to_string(self->value_);
                esp_err_t err = bsp_hotknot_send(msg.c_str(), msg.size(), 3000);
                lv_label_set_text(self->status_label_, err == ESP_OK ? "Sent" : "Send failed");
                bsp_hotknot_end();
            });
            break;
        case BSP_HOTKNOT_EVENT_ERROR:
            lv_async_call([self]{ lv_label_set_text(self->status_label_, "Error"); });
            break;
        default:
            break;
        }
    }, this);
}

void ShareScreen::onDisappear() {
    bsp_hotknot_end();
}
