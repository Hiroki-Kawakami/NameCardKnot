/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ShareScreen.hpp"
#include "screen_manager.hpp"
#include "lv_image_adapter.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHARE_IMAGE_W 203
#define SHARE_IMAGE_H 360

ShareScreen::ShareScreen(std::shared_ptr<SharedCardData> data) : data_(std::move(data)) {}

ShareScreen::~ShareScreen() {
    stopWorker();
    if (poll_timer_) lv_timer_delete(poll_timer_);
}

void ShareScreen::build() {
    createNavigation("Share");
    lv_obj_set_style_border_width(navigation_, 0, 0);
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    share_images_ = lv_container_create(contents_, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(share_images_, SHARE_IMAGE_H);
    lv_obj_set_style_pad_column(share_images_, 20, 0);

    if (!data_ || !data_->valid()) return;
    int n = data_->share_image_count();
    if (n <= 0) return;

    slots_.reserve(n);
    for (int i = 0; i < n; i++) slots_.push_back(std::make_unique<Slot>());

    startWorker();
    poll_timer_ = lv_timer_create([](lv_timer_t *t) {
        static_cast<ShareScreen *>(lv_timer_get_user_data(t))->poll();
    }, 100, this);
}

void ShareScreen::onAppear() {
}

void ShareScreen::onDisappear() {
}

void ShareScreen::back() {
    stopWorker();
    if (poll_timer_) { lv_timer_delete(poll_timer_); poll_timer_ = nullptr; }
    screen_manager.pop();
}

void ShareScreen::worker() {
    imgproc::Options o;
    o.target_w = SHARE_IMAGE_W;
    o.target_h = SHARE_IMAGE_H;
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

void ShareScreen::workerTask(void *arg) {
    static_cast<ShareScreen *>(arg)->worker();
    vTaskDelete(nullptr);
}

void ShareScreen::startWorker() {
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

void ShareScreen::stopWorker() {
    if (!worker_started_) return;
    stop_.store(true);
    while (worker_running_.load()) vTaskDelay(pdMS_TO_TICKS(2));
    worker_started_ = false;
}

void ShareScreen::poll() {
    size_t resolved = 0;
    for (auto &slot : slots_) {
        int st = slot->state.load(std::memory_order_acquire);
        if (st == 0) continue;
        resolved++;
        if (st == 1 && !slot->shown) {
            slot->shown = true;
            lv_image_set_src(lv_image_create(share_images_), &slot->dsc);
        }
    }
    if (resolved >= slots_.size()) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}
