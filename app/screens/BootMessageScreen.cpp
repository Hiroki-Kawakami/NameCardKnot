/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "BootMessageScreen.hpp"
#include "widgets.hpp"
#include "NameCardKnot.hpp"
#include "Power.hpp"
#include "bsp.h"

BootMessageScreen::BootMessageScreen(bootmsg::Id id, std::string param, Mode mode,
                                      std::function<void()> on_continue)
    : id_(id), param_(std::move(param)), mode_(mode), on_continue_(std::move(on_continue)) {}

void BootMessageScreen::build() {
    modal_ = lv_modal_open(root_);

    const char *title = "Notice";
    if (id_ == bootmsg::Id::HotKnotShareFailed) title = "Share Failed";
    else if (id_ == bootmsg::Id::HotKnotReceiveFailed) title = "Receive Failed";
    lv_modal_title_create(modal_, title);
    lv_modal_message_create(modal_, param_.c_str());

    if (mode_ == Mode::ResetFailed) {
        lv_modal_message_create(modal_, "Press the reset button on the back of the device to restart.");
    } else {
        lv_modal_button_create(modal_, "OK", LV_MODAL_BUTTON_TYPE_PRIMARY, [this](lv_event_t*) {
            lv_async_call([this]() {
                bsp_display_clear();
                if (on_continue_) on_continue_();
            });
        });
    }
}

void BootMessageScreen::onAppear() {
    if (mode_ == Mode::Boot) {
        epd_set_next_refresh_mode(BSP_EPD_MODE_NONE);   // caller drives the modal rect after load
    } else {
        power::set_timeout(this, 0);   // the sleep flow would draw over the message
    }
}

void BootMessageScreen::refreshModal() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
    lv_obj_invalidate(modal_);
}
