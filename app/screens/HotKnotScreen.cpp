/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "HotKnotScreen.hpp"
#include "screen_manager.hpp"
#include "lv_image_adapter.hpp"
#include "NameCardKnot.hpp"
#include "BootMessageScreen.hpp"
#include "Strings.hpp"
#include "UiFont.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <string>

static const char *TAG = "hotknot";

static constexpr uint32_t kHotKnotSendTimeoutMs = 5000;
static constexpr uint32_t kSlaveReceiveTimeoutMs = 10000;

static std::string hotknot_error_message(esp_err_t err) {
    const char *text = err == ESP_ERR_TIMEOUT ? S().devices_separated : S().card_exchange_failed;
    char buf[160];
    snprintf(buf, sizeof buf, S().error_detail_fmt, text, esp_err_to_name(err));
    return buf;
}

HotKnotScreen::HotKnotScreen(std::shared_ptr<SharedCardData> data) : data_(std::move(data)) {}

HotKnotScreen::~HotKnotScreen() {
    stopWorker();
    endHotKnot();
    if (poll_timer_) lv_timer_delete(poll_timer_);
}

void HotKnotScreen::back() {
    stopWorker();
    endHotKnot();
    if (poll_timer_) { lv_timer_delete(poll_timer_); poll_timer_ = nullptr; }
    screen_manager.pop();
}

const lv_font_t *HotKnotScreen::nameFont() {
    if (!name_font_) name_font_ = std::make_unique<NameFont>(data_ ? data_->name_glyphs() : nullptr);
    return name_font_->font();
}

void HotKnotScreen::createHotKnotStartMessage(lv_obj_t *parent) {
    hotknot_start_msg_ = lv_container_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(hotknot_start_msg_, 1, 0);
    lv_obj_set_style_border_color(hotknot_start_msg_, lv_color_black(), 0);
    lv_obj_set_style_border_side(hotknot_start_msg_, (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
    lv_obj_set_flex_grow(hotknot_start_msg_, 1);
    lv_obj_set_width(hotknot_start_msg_, LV_PCT(100));
    lv_obj_set_style_pad_row(hotknot_start_msg_, 20, 0);

    auto icon = lv_label_create(hotknot_start_msg_);
    lv_label_set_text(icon, LUCIDE_SMARTPHONE_NFC);
    lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);

    auto msg = lv_label_create(hotknot_start_msg_);
    lv_label_set_text(msg, S().hold_screen_hint);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, LV_PCT(100));
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(msg, ui_font_24(), 0);
}

lv_obj_t *HotKnotScreen::createShareImages(lv_obj_t *parent) {
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
            static_cast<HotKnotScreen *>(lv_timer_get_user_data(t))->poll();
        }, 100, this);
    }
    return share_images_;
}

void HotKnotScreen::worker() {
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

void HotKnotScreen::workerTask(void *arg) {
    static_cast<HotKnotScreen *>(arg)->worker();
    vTaskDelete(nullptr);
}

void HotKnotScreen::startWorker() {
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

void HotKnotScreen::stopWorker() {
    if (!worker_started_) return;
    stop_.store(true);
    while (worker_running_.load()) vTaskDelay(pdMS_TO_TICKS(2));
    worker_started_ = false;
}

void HotKnotScreen::poll() {
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

// MARK: HotKnot session

void HotKnotScreen::hotKnotEvent(const bsp_hotknot_event_t *ev, void *arg) {
    auto *self = static_cast<HotKnotScreen *>(arg);
    switch (ev->type) {
        case BSP_HOTKNOT_EVENT_PAIRED:
            self->hk_state_.store(HkState::Paired, std::memory_order_release);
            break;
        case BSP_HOTKNOT_EVENT_READY:
            self->hk_state_.store(HkState::Ready, std::memory_order_release);
            break;
        case BSP_HOTKNOT_EVENT_RECEIVED:
            self->stashReceived(ev->data, ev->len);
            self->hk_state_.store(HkState::Done, std::memory_order_release);
            break;
        case BSP_HOTKNOT_EVENT_ERROR:
            self->hk_err_ = ev->err;
            self->hk_state_.store(HkState::Failed, std::memory_order_release);
            break;
    }
}

bool HotKnotScreen::startHotKnot(bsp_hotknot_role_t role) {
    esp_err_t err = bsp_hotknot_begin(role, hotKnotEvent, this);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "hotknot begin failed: %s", esp_err_to_name(err));
        return false;
    }
    role_ = role;
    hk_active_ = true;
    hk_seen_ = HkState::Approaching;
    hk_state_.store(HkState::Approaching, std::memory_order_release);
    hk_timer_ = lv_timer_create([](lv_timer_t *t) {
        static_cast<HotKnotScreen *>(lv_timer_get_user_data(t))->pollHotKnot();
    }, 100, this);
    return true;
}

void HotKnotScreen::endHotKnot() {
    if (!hk_active_) return;
    hk_active_ = false;
    while (hk_send_busy_.load(std::memory_order_acquire)) vTaskDelay(pdMS_TO_TICKS(2));
    bsp_hotknot_end();
    if (hk_timer_) { lv_timer_delete(hk_timer_); hk_timer_ = nullptr; }
}

void HotKnotScreen::sendTask(void *arg) {
    auto *self = static_cast<HotKnotScreen *>(arg);
    esp_err_t err = bsp_hotknot_send(self->hk_send_data_, self->hk_send_len_, kHotKnotSendTimeoutMs);
    if (err != ESP_OK) self->hk_err_ = err;
    self->hk_send_busy_.store(false, std::memory_order_relaxed);
    self->hk_state_.store(err == ESP_OK ? HkState::Done : HkState::Failed, std::memory_order_release);
    vTaskDelete(nullptr);
}

void HotKnotScreen::sendHotKnot(const void *data, size_t len) {
    hk_send_data_ = data;
    hk_send_len_ = len;
    hk_send_busy_.store(true, std::memory_order_relaxed);
    if (xTaskCreate(sendTask, "hksend", 4096, this, uxTaskPriorityGet(nullptr), nullptr) != pdPASS) {
        hk_send_busy_.store(false, std::memory_order_relaxed);
        failHotKnot(ESP_FAIL);
    }
}

void HotKnotScreen::failHotKnot(esp_err_t err) {
    hk_err_ = err;
    hk_state_.store(HkState::Failed, std::memory_order_release);
}

void HotKnotScreen::pollHotKnot() {
    HkState s = hk_state_.load(std::memory_order_acquire);
    while (hk_seen_ != s) {
        switch (hk_seen_) {
            case HkState::Idle:
            case HkState::Approaching:
                hk_seen_ = HkState::Paired; onHotKnotPaired(); break;
            case HkState::Paired:
                if (s == HkState::Failed) { hk_seen_ = HkState::Failed; onHotKnotFailed(hk_err_); }
                else { hk_seen_ = HkState::Ready; ready_tick_ = lv_tick_get(); onHotKnotReady(); }
                break;
            case HkState::Ready:
                if (s == HkState::Failed) { hk_seen_ = HkState::Failed; onHotKnotFailed(hk_err_); }
                else {
                    hk_seen_ = HkState::Done;
                    // Audible cue: the screens face each other, so this is the
                    // only feedback that they can be pulled apart now.
                    bsp_audio_tone(2700, 150);
                    onHotKnotDone();
                }
                break;
            default: return;
        }
    }
    // Master's send has its own bsp_hotknot_send timeout; the slave just waits
    // for RECEIVED, so it needs its own watchdog here.
    if (hk_seen_ == HkState::Ready && role_ == BSP_HOTKNOT_ROLE_SLAVE &&
        lv_tick_elaps(ready_tick_) >= kSlaveReceiveTimeoutMs) {
        failHotKnot(ESP_ERR_TIMEOUT);
    }
}

void HotKnotScreen::onHotKnotPaired() {
    stopWorker();
    if (poll_timer_) { lv_timer_delete(poll_timer_); poll_timer_ = nullptr; }
    slots_.clear();
    share_images_ = nullptr;
    hotknot_start_msg_ = nullptr;
    modal_ = nullptr;
    modal_message_ = nullptr;
    navigation_ = nullptr;
    navigation_title_ = nullptr;
    contents_ = nullptr;

    lv_obj_clean(root_);
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);

    auto session = lv_container_create(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(session, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(session, 20, 0);
    lv_obj_set_style_pad_row(session, 20, 0);

    auto title = lv_label_create(session);
    lv_label_set_text(title, S().hotknot_approach);
    lv_obj_set_style_text_font(title, ui_font_48(), 0);

    auto icon = lv_label_create(session);
    lv_label_set_text(icon, LUCIDE_SMARTPHONE_NFC);
    lv_obj_set_style_text_font(icon, R.font.lucide_40, 0);

    session_msg_ = lv_label_create(session);
    lv_label_set_text(session_msg_, S().keep_held_hint);
    lv_label_set_long_mode(session_msg_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(session_msg_, LV_PCT(100));
    lv_obj_set_style_text_align(session_msg_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(session_msg_, ui_font_24(), 0);

    paired_ = true;
}

void HotKnotScreen::setSessionMessage(const char *message) {
    if (!session_msg_) return;
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
    lv_label_set_text(session_msg_, message);
}

void HotKnotScreen::onHotKnotFailed(esp_err_t err) {
    ESP_LOGW(TAG, "hotknot failed: %s", esp_err_to_name(err));
    if (!paired_) {
        endHotKnot();
        if (!modal_) showProgressModal("");
        setProgressMessage(S().connection_failed_retry);
        addModalCloseButton();
        return;
    }
    failAndReboot(hotknot_error_message(err).c_str());
}

void HotKnotScreen::failAndReboot(const char *message) {
    if (hk_timer_) { lv_timer_delete(hk_timer_); hk_timer_ = nullptr; }
    bootmsg::save(bootMsgId(), message);

    bsp_display_wait_idle();
    esp_err_t err = bsp_hw_reset();
    ESP_LOGE(TAG, "hw reset failed: %s", esp_err_to_name(err));

    // Still here: USB kept VSYS up. Show the message directly instead of at boot.
    bootmsg::clear();
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT_ALL);
    screen_manager.load(std::make_shared<BootMessageScreen>(
        bootMsgId(), message, BootMessageScreen::Mode::ResetFailed));
}

// MARK: Progress / result modal

void HotKnotScreen::showProgressModal(const char *message) {
    if (modal_) return;
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
    modal_ = lv_modal_open(root_);
    lv_modal_title_create(modal_, transferTitle());
    modal_message_ = lv_modal_message_create(modal_, message);
}

void HotKnotScreen::setProgressMessage(const char *message) {
    if (!modal_message_) return;
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
    lv_label_set_text(modal_message_, message);
}

void HotKnotScreen::addModalCloseButton(const char *text) {
    if (!modal_) return;
    epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
    lv_modal_button_create(modal_, text ? text : S().close, LV_MODAL_BUTTON_TYPE_PRIMARY, [this](lv_event_t *) {
        lv_async_call([this]() {
            epd_set_next_refresh_mode(BSP_EPD_MODE_TEXT);
            back();
        });
    });
}
