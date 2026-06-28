/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "ReceiveScreen.hpp"
#include "NameCardKnot.hpp"
#include "screen_manager.hpp"
#include "lv_image_adapter.hpp"
#include "resources.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHARE_IMAGE_W 203
#define SHARE_IMAGE_H 360

ReceiveScreen::ReceiveScreen(std::shared_ptr<SharedCardData> data) : data_(std::move(data)) {}

ReceiveScreen::~ReceiveScreen() {
    stopWorker();
    if (poll_timer_) lv_timer_delete(poll_timer_);
}

void ReceiveScreen::build() {
    createNavigation("Receive");
    lv_obj_set_style_border_width(navigation_, 0, 0);
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(contents_, 20, 0);
    lv_obj_set_style_pad_row(contents_, 20, 0);
    lv_obj_set_style_pad_bottom(contents_, 20, 0);

    {
        auto container = lv_container_create(contents_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(container, 1, 0);
        lv_obj_set_style_border_color(container, lv_color_black(), 0);
        lv_obj_set_style_border_side(container, (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
        lv_obj_set_flex_grow(container, 1);
        lv_obj_set_width(container, LV_PCT(100));
        lv_obj_set_style_pad_row(container, 20, 0);

        auto icon = lv_label_create(container);
        lv_label_set_text(icon, LUCIDE_SMARTPHONE_NFC);
        lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);

        auto msg = lv_label_create(container);
        lv_label_set_text(msg, "Hold this screen against the other device's screen to start sharing.");
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_24, 0);
    }

    if (data_) {
        loadShareCardData();
    }
}

void ReceiveScreen::onAppear() {
}

void ReceiveScreen::onDisappear() {
}

void ReceiveScreen::loadShareCardData() {
    if (!data_ || !data_->valid()) return;

    auto button = lv_button_create(contents_);
    lv_obj_remove_style_all(button);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_width(button, 2, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(button, 1, 0);
    lv_obj_set_style_pad_all(button, 0, LV_STATE_PRESSED);
    lv_obj_set_style_flex_flow(button, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    {
        auto container = lv_container_create(button, LV_FLEX_FLOW_ROW);
        lv_obj_center(container);
        lv_obj_set_size(container, 500, 64);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(container, 20, 0);
        lv_obj_set_style_pad_hor(container, 20, 0);
        lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);

        auto createCheckbox = [this, container](lv_event_t *e) {
            if (e) return_my_data_ = !return_my_data_;

            lv_obj_clean(container);
            auto checkbox = lv_label_create(container);
            lv_obj_set_style_text_font(checkbox, &lv_font_montserrat_24, 0);
            lv_obj_set_size(checkbox, 24, 24);
            lv_obj_set_style_border_width(checkbox, 1, 0);
            if (return_my_data_) {
                lv_label_set_text(checkbox, LV_SYMBOL_OK);
                lv_obj_set_style_border_color(checkbox, lv_color_white(), 0);
            } else {
                lv_label_set_text(checkbox, "");
                lv_obj_set_style_border_color(checkbox, lv_color_black(), 0);
            }

            auto label = lv_label_create(container);
            lv_label_set_text(label, "Also send my card in return");
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        };
        createCheckbox(nullptr);
        lv_obj_add_event_fn(button, LV_EVENT_CLICKED, createCheckbox);
    }

    if (data_ && data_->valid() && !data_->name().empty()) {
        name_font_ = std::make_unique<NameFont>(data_->name_glyphs());
        lv_obj_t *name = lv_label_create(button);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_text(name, data_->name().c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(name, name_font_->font(), 0);
        lv_obj_set_style_pad_hor(name, 20, 0);
        lv_obj_set_style_pad_bottom(name, 16, 0);
    }

    share_images_ = lv_container_create(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(share_images_, SHARE_IMAGE_H);
    lv_obj_set_style_pad_column(share_images_, 20, 0);
    lv_obj_remove_flag(share_images_, LV_OBJ_FLAG_CLICKABLE);

    int n = data_->share_image_count();
    if (n > 0) {
        slots_.reserve(n);
        for (int i = 0; i < n; i++) {
            auto slot = std::make_unique<Slot>();
            slot->obj = lv_image_create(share_images_);
            lv_obj_set_size(slot->obj, SHARE_IMAGE_W, SHARE_IMAGE_H);
            slots_.push_back(std::move(slot));
        }

        startWorker();
        poll_timer_ = lv_timer_create([](lv_timer_t *t) {
            static_cast<ReceiveScreen *>(lv_timer_get_user_data(t))->poll();
        }, 100, this);
    }
}

void ReceiveScreen::back() {
    stopWorker();
    if (poll_timer_) { lv_timer_delete(poll_timer_); poll_timer_ = nullptr; }
    screen_manager.pop();
}

void ReceiveScreen::worker() {
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

void ReceiveScreen::workerTask(void *arg) {
    static_cast<ReceiveScreen *>(arg)->worker();
    vTaskDelete(nullptr);
}

void ReceiveScreen::startWorker() {
    worker_running_.store(true);
    worker_started_ = true;
#ifdef ESP_PLATFORM
    BaseType_t core = 1 - xPortGetCoreID();
    UBaseType_t prio = uxTaskPriorityGet(nullptr);
#else
    BaseType_t core = tskNO_AFFINITY;
    UBaseType_t prio = 1;
#endif
    if (xTaskCreatePinnedToCore(workerTask, "recvimg", 16384, this, prio, nullptr, core) != pdPASS)
        worker();  // synchronous fallback (clears worker_running_)
}

void ReceiveScreen::stopWorker() {
    if (!worker_started_) return;
    stop_.store(true);
    while (worker_running_.load()) vTaskDelay(pdMS_TO_TICKS(2));
    worker_started_ = false;
}

void ReceiveScreen::poll() {
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
