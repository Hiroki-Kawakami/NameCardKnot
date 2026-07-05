/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "BootMessageScreen.hpp"
#include "widgets.hpp"
#include "NameCardKnot.hpp"
#include "SharedCardData.hpp"
#include "SharedCardScreen.hpp"
#include "Power.hpp"
#include "Strings.hpp"
#include "bsp.h"

BootMessageScreen::BootMessageScreen(bootmsg::Id id, std::string param, Mode mode,
                                      std::function<void()> on_continue)
    : id_(id), param_(std::move(param)), mode_(mode), on_continue_(std::move(on_continue)) {}

void BootMessageScreen::build() {
    modal_ = lv_modal_open(root_);

    const char *title = S().notice;
    if (id_ == bootmsg::Id::ShareFailed) title = S().share_failed;
    else if (id_ == bootmsg::Id::ReceiveFailed) title = S().receive_failed;
    else if (id_ == bootmsg::Id::TransferFailed) title = S().transfer_failed;
    else if (id_ == bootmsg::Id::TransferComplete) title = S().transfer_complete;
    lv_modal_title_create(modal_, title);
    lv_modal_message_create(modal_, param_.c_str());

    if (mode_ == Mode::ResetFailed) {
        lv_modal_message_create(modal_, S().press_reset_hint);
        return;
    }

    auto back = [this](lv_event_t*) {
        lv_async_call([this]() {
            bsp_display_clear();
            if (on_continue_) on_continue_();
        });
    };

    if (id_ == bootmsg::Id::TransferComplete) received_path_ = lastcard::take_received();
    if (!received_path_.empty()) {
        lv_modal_button_create(modal_, S().open_card, LV_MODAL_BUTTON_TYPE_PRIMARY, [this](lv_event_t*) {
            lv_async_call([this]() {
                bsp_display_clear();
                mount_sd_card();
                auto data = SharedCardData::open(received_path_);
                if (data && data->valid())
                    screen_manager.load(std::make_shared<SharedCardScreen>(data, SharedCardScreen::Nav::Received));
                else if (on_continue_)
                    on_continue_();
            });
        });
        lv_modal_button_create(modal_, S().back, LV_MODAL_BUTTON_TYPE_PRIMARY, back);
    } else {
        lv_modal_button_create(modal_, id_ == bootmsg::Id::TransferComplete ? S().back : S().ok,
                                LV_MODAL_BUTTON_TYPE_PRIMARY, back);
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
#if CONFIG_IDF_TARGET_ESP32S3
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
#else
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_ALL);
#endif
    lv_obj_invalidate(modal_);
}
