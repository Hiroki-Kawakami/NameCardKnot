/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ShareScreen.hpp"
#include <cstdlib>
#include "NameCardKnot.hpp"
#include "MyCardStore.hpp"

void ShareScreen::build() {
    createNavigation("Dokan Send");
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    status_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_48, 0);
    lv_label_set_text(status_label_, "----");

    value_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(value_label_, &lv_font_montserrat_24, 0);
    lv_label_set_text(value_label_, "");
}

void ShareScreen::tick() {
    lv_label_set_text(status_label_, status_);
    lv_label_set_text_fmt(value_label_, "%u / %u", (unsigned)sent_, (unsigned)len_);
}

void ShareScreen::onAppear() {
    finished_ = false;
    off_ = 0;
    stream_ = nullptr;
    sent_ = 0;
    status_ = "Connecting";
    lv_label_set_text(status_label_, status_);
    lv_label_set_text(value_label_, "");

    if (!mycard::Store::mount() || !mycard::Store::available()) {
        lv_label_set_text(status_label_, "No card");
        return;
    }
    len_ = mycard::Store::blob_len(mycard::BLOB_PDF);
    buf_ = (uint8_t *)malloc(len_);
    if (!buf_ || !mycard::Store::read_blob(mycard::BLOB_PDF, buf_, len_)) {
        lv_label_set_text(status_label_, "Read failed");
        free(buf_);
        buf_ = nullptr;
        return;
    }

    dokan_config_t cfg = {};
    cfg.connect_timeout_ms = 30000;
    esp_err_t err = dokan_open(DOKAN_TEST_DESCRIPTOR, DOKAN_ROLE_HOST, DOKAN_APP_ID, &cfg,
                               &ShareScreen::onEvent, this, &session_);
    if (err != ESP_OK) {
        lv_label_set_text_fmt(status_label_, "open: %d", err);
        free(buf_);
        buf_ = nullptr;
        return;
    }
    ui_timer_ = lv_timer_create([](lv_timer_t *t) {
        static_cast<ShareScreen *>(lv_timer_get_user_data(t))->tick();
    }, 500, this);
}

void ShareScreen::pumpSend() {
    if (finished_ || !stream_) return;
    while (off_ < len_) {
        size_t w = 0;
        dokan_stream_write(stream_, buf_ + off_, len_ - off_, &w);
        if (w == 0) break;  // send window full; wait for STREAM_WRITABLE
        off_ += (uint32_t)w;
    }
    sent_ = off_;
    if (off_ >= len_) {
        dokan_stream_finish(stream_);
        finished_ = true;
        status_ = "Sent";
    }
}

void ShareScreen::onEvent(const dokan_event_t *ev, void *arg) {
    auto self = static_cast<ShareScreen *>(arg);
    switch (ev->type) {
    case DOKAN_EVENT_CONNECTED: {
        self->status_ = "Sending";
        dokan_stream_opts_t opts = { 0, self->len_ };
        if (dokan_stream_open(ev->session, &opts, &self->stream_) == ESP_OK) self->pumpSend();
        break;
    }
    case DOKAN_EVENT_STREAM_WRITABLE:
        self->pumpSend();
        break;
    case DOKAN_EVENT_ERROR:
        self->status_ = "Error";
        break;
    default:
        break;
    }
}

void ShareScreen::onDisappear() {
    if (ui_timer_) {
        lv_timer_delete(ui_timer_);
        ui_timer_ = nullptr;
    }
    if (session_) {
        dokan_close(session_);  // joins the I/O task: no events after this
        session_ = nullptr;
    }
    free(buf_);
    buf_ = nullptr;
    stream_ = nullptr;
}
