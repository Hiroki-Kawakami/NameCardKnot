/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "widgets.hpp"
#include "dokan.h"

class ShareScreen : public NavigationScreen {
public:
    void build() override;
    void onAppear() override;
    void onDisappear() override;

private:
    static void onEvent(const dokan_event_t *ev, void *arg);  // dokan I/O task
    void pumpSend();                                          // dokan I/O task
    void tick();                                              // LVGL thread (lv_timer)

    lv_obj_t   *value_label_;
    lv_obj_t   *status_label_;
    lv_timer_t *ui_timer_ = nullptr;

    dokan_session_t *session_ = nullptr;
    dokan_stream_t  *stream_ = nullptr;
    uint8_t  *buf_ = nullptr;
    uint32_t  len_ = 0;
    uint32_t  off_ = 0;
    bool      finished_ = false;

    // written by the I/O task, read by the lv_timer — no LVGL calls off-thread
    const char *volatile status_ = "";
    volatile uint32_t sent_ = 0;
};
