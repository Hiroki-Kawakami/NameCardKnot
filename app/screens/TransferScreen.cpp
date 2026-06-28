/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "TransferScreen.hpp"
#include "screen_manager.hpp"
#include "lv_image_adapter.hpp"
#include "NameCardKnot.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

TransferScreen::TransferScreen(std::shared_ptr<SharedCardData> data) : data_(std::move(data)) {}

TransferScreen::~TransferScreen() {
    stopWorker();
    if (poll_timer_) lv_timer_delete(poll_timer_);
}

void TransferScreen::back() {
    stopWorker();
    if (poll_timer_) { lv_timer_delete(poll_timer_); poll_timer_ = nullptr; }
    screen_manager.pop();
}

const lv_font_t *TransferScreen::nameFont() {
    if (!name_font_) name_font_ = std::make_unique<NameFont>(data_ ? data_->name_glyphs() : nullptr);
    return name_font_->font();
}

lv_obj_t *TransferScreen::createShareImages(lv_obj_t *parent) {
    share_images_ = lv_container_create(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(share_images_, kShareImageH);
    lv_obj_set_style_pad_column(share_images_, 20, 0);
    lv_obj_remove_flag(share_images_, LV_OBJ_FLAG_CLICKABLE);

    int n = data_ ? data_->share_image_count() : 0;
    if (n > 0) {
        // Pre-create the fixed-size placeholders so the row layout is final up
        // front: filling one later never reflows the others (each reflow is an
        // EPD refresh of every prior image).
        slots_.reserve(n);
        for (int i = 0; i < n; i++) {
            auto slot = std::make_unique<Slot>();
            slot->obj = lv_image_create(share_images_);
            lv_obj_set_size(slot->obj, kShareImageW, kShareImageH);
            slots_.push_back(std::move(slot));
        }

        startWorker();
        poll_timer_ = lv_timer_create([](lv_timer_t *t) {
            static_cast<TransferScreen *>(lv_timer_get_user_data(t))->poll();
        }, 100, this);
    }
    return share_images_;
}

void TransferScreen::worker() {
    imgproc::Options o;
    o.target_w = kShareImageW;
    o.target_h = kShareImageH;
    o.fit = imgproc::Fit::Contain;
    o.levels = 16;

    for (size_t i = 0; i < slots_.size(); i++) {
        if (stop_.load()) break;  // interrupted at an image boundary
        imgproc::Image img;
        imgproc::Status st = data_->decode_share_image((int)i, o, img);
        if (stop_.load()) break;  // interrupted mid-decode: drop the result
        Slot *s = slots_[i].get();
        if (st == imgproc::Status::Ok) {
            s->img = std::move(img);
            imgproc_fill_lv_dsc(s->img, s->dsc);
            s->state.store(1, std::memory_order_release);
        } else {
            s->state.store(2, std::memory_order_release);
        }
    }
    worker_running_.store(false);
}

void TransferScreen::workerTask(void *arg) {
    static_cast<TransferScreen *>(arg)->worker();
    vTaskDelete(nullptr);
}

void TransferScreen::startWorker() {
    worker_running_.store(true);
    worker_started_ = true;
#ifdef ESP_PLATFORM
    BaseType_t core = 1 - xPortGetCoreID();
    UBaseType_t prio = uxTaskPriorityGet(nullptr);
#else
    BaseType_t core = tskNO_AFFINITY;
    UBaseType_t prio = 1;
#endif
    if (xTaskCreatePinnedToCore(workerTask, "shareimg", 16384, this, prio, nullptr, core) != pdPASS)
        worker();  // synchronous fallback (clears worker_running_)
}

void TransferScreen::stopWorker() {
    if (!worker_started_) return;
    stop_.store(true);
    while (worker_running_.load()) vTaskDelay(pdMS_TO_TICKS(2));
    worker_started_ = false;
}

void TransferScreen::poll() {
    size_t resolved = 0;
    for (auto &slot : slots_) {
        int st = slot->state.load(std::memory_order_acquire);
        if (st == 0) continue;
        resolved++;
        if (st == 1 && !slot->shown) {
            slot->shown = true;
            epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
            lv_image_set_src(slot->obj, &slot->dsc);
        }
    }
    if (resolved >= slots_.size()) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}
