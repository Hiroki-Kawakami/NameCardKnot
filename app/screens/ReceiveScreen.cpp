/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ReceiveScreen.hpp"
#include <string>
#include "bsp.h"

void ReceiveScreen::build() {
    createNavigation("HotKnot Receive");
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    value_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(value_label_, &lv_font_montserrat_48, 0);
    lv_label_set_text(value_label_, "----");

    status_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_24, 0);
    lv_label_set_text(status_label_, "");
}

void ReceiveScreen::onAppear() {
    lv_label_set_text(value_label_, "----");
    lv_label_set_text(status_label_, "Approaching...");

    bsp_hotknot_begin(BSP_HOTKNOT_ROLE_SLAVE, [](const bsp_hotknot_event_t *ev, void *arg) {
        auto self = (ReceiveScreen*)arg;
        switch (ev->type) {
        case BSP_HOTKNOT_EVENT_PAIRED:
            lv_async_call([self]{ lv_label_set_text(self->status_label_, "Paired..."); });
            break;
        case BSP_HOTKNOT_EVENT_RECEIVED: {
            std::string msg((const char*)ev->data, ev->len);
            lv_async_call([self, msg]{
                lv_label_set_text(self->value_label_, msg.c_str());
                lv_label_set_text(self->status_label_, "Received");
                bsp_hotknot_end();
            });
            break;
        }
        case BSP_HOTKNOT_EVENT_ERROR:
            lv_async_call([self]{ lv_label_set_text(self->status_label_, "Error"); });
            break;
        default:
            break;
        }
    }, this);
}

void ReceiveScreen::onDisappear() {
    bsp_hotknot_end();
}
