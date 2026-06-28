/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ReceiveScreen.hpp"
#include "NameCardKnot.hpp"
#include "namecard_pdf.hpp"
#include "lv_image_adapter.hpp"
#include <cstdlib>
#include <cstring>

void ReceiveScreen::build() {
    createNavigation("Dokan Receive");
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    status_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_48, 0);
    lv_label_set_text(status_label_, "----");

    value_label_ = lv_label_create(contents_);
    lv_obj_set_style_text_font(value_label_, &lv_font_montserrat_24, 0);
    lv_label_set_text(value_label_, "");
}

void ReceiveScreen::freePdf() {
    free(pdf_);
    pdf_ = nullptr;
}

void ReceiveScreen::tick() {
    lv_label_set_text(status_label_, status_);
    lv_label_set_text_fmt(value_label_, "%u / %u", (unsigned)received_, (unsigned)expected_);
    if (recv_done_.load() && !displayed_) {
        displayed_ = true;
        showImage();
    }
}

void ReceiveScreen::showImage() {
    if (!pdf_) { status_ = "No data"; return; }

    nckpdf::Card card;
    if (nckpdf::parse_buffer(pdf_, received_, card) != nckpdf::Status::Ok) {
        status_ = "Bad PDF";
        freePdf();
        return;
    }
    const nckpdf::Asset *a = card.find(nckpdf::AssetType::ShareJpeg);
    if (!a) a = card.find(nckpdf::AssetType::DisplayJpeg);
    if (!a) {
        status_ = "No image";
        freePdf();
        return;
    }

    lv_obj_update_layout(contents_);
    int box = lv_obj_get_content_width(contents_);
    if (box <= 0) box = 480;

    imgproc::Options opts;
    opts.target_w = (uint16_t)box;
    opts.target_h = (uint16_t)box;  // Contain keeps aspect inside the box
    opts.fit = imgproc::Fit::Contain;
    if (imgproc::decode_buffer(pdf_ + a->offset, a->length, opts, image_) != imgproc::Status::Ok) {
        status_ = "Decode failed";
        freePdf();
        return;
    }
    freePdf();  // the JPEG is decoded; the raw PDF is no longer needed

    if (imgproc_fill_lv_dsc(image_, dsc_)) {
        image_obj_ = lv_image_create(contents_);
        lv_image_set_src(image_obj_, &dsc_);
    }
    status_ = "Received";
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);
}

void ReceiveScreen::onAppear() {
    received_ = 0;
    expected_ = 0;
    displayed_ = false;
    recv_done_.store(false);
    status_ = "Connecting";
    lv_label_set_text(status_label_, status_);
    lv_label_set_text(value_label_, "");

    dokan_config_t cfg = {};
    cfg.connect_timeout_ms = 30000;
    esp_err_t err = dokan_open(DOKAN_TEST_DESCRIPTOR, DOKAN_ROLE_CLIENT, DOKAN_APP_ID, &cfg,
                               &ReceiveScreen::onEvent, this, &session_);
    if (err != ESP_OK) {
        lv_label_set_text_fmt(status_label_, "open: %d", err);
        return;
    }
    ui_timer_ = lv_timer_create([](lv_timer_t *t) {
        static_cast<ReceiveScreen *>(lv_timer_get_user_data(t))->tick();
    }, 500, this);
}

void ReceiveScreen::onEvent(const dokan_event_t *ev, void *arg) {
    auto self = static_cast<ReceiveScreen *>(arg);
    switch (ev->type) {
    case DOKAN_EVENT_CONNECTED:
        self->status_ = "Receiving";
        break;
    case DOKAN_EVENT_STREAM_OPENED:
        self->expected_ = (uint32_t)ev->opts.size_hint;
        self->pdf_cap_ = (uint32_t)ev->opts.size_hint;
        if (self->pdf_cap_) self->pdf_ = (uint8_t *)malloc(self->pdf_cap_);
        break;
    case DOKAN_EVENT_STREAM_DATA:
        if (self->pdf_ && self->received_ + ev->len <= self->pdf_cap_)
            memcpy(self->pdf_ + self->received_, ev->data, ev->len);
        self->received_ += (uint32_t)ev->len;
        break;
    case DOKAN_EVENT_STREAM_FINISHED:
        self->status_ = "Decoding";
        self->recv_done_.store(true);  // publishes the pdf_ bytes to the UI thread
        break;
    case DOKAN_EVENT_ERROR:
        self->status_ = "Error";
        break;
    default:
        break;
    }
}

void ReceiveScreen::onDisappear() {
    if (ui_timer_) {
        lv_timer_delete(ui_timer_);
        ui_timer_ = nullptr;
    }
    if (session_) {
        dokan_close(session_);  // joins the I/O task: no events after this
        session_ = nullptr;
    }
    freePdf();
}
