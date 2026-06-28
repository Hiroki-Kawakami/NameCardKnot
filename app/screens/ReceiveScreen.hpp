/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "widgets.hpp"
#include "dokan.h"
#include "image_processor.hpp"
#include <atomic>

class ReceiveScreen : public NavigationScreen {
public:
    void build() override;
    void onAppear() override;
    void onDisappear() override;

private:
    static void onEvent(const dokan_event_t *ev, void *arg);  // dokan I/O task
    void tick();       // LVGL thread (lv_timer)
    void showImage();  // LVGL thread: parse PDF + decode share JPEG + display
    void freePdf();

    lv_obj_t   *value_label_;
    lv_obj_t   *status_label_;
    lv_obj_t   *image_obj_ = nullptr;
    lv_timer_t *ui_timer_ = nullptr;
    dokan_session_t *session_ = nullptr;

    uint8_t  *pdf_ = nullptr;   // received .mnc.pdf (written by the I/O task)
    uint32_t  pdf_cap_ = 0;

    // written by the I/O task, read by the lv_timer
    const char *volatile status_ = "";
    volatile uint32_t received_ = 0;
    volatile uint32_t expected_ = 0;
    std::atomic<bool> recv_done_{false};  // release/acquire for the pdf_ bytes
    bool displayed_ = false;

    // decoded share image, kept alive while the lv_image references it
    imgproc::Image image_;
    lv_image_dsc_t dsc_{};
};
