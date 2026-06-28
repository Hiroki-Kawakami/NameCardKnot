/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "widgets.hpp"
#include "SharedCardData.hpp"
#include "NameFont.hpp"
#include "image_processor.hpp"
#include "bsp.h"
#include <atomic>
#include <memory>
#include <vector>

// Base for the card-transfer screens (ShareScreen / ReceiveScreen): the peer card
// (data_), its name font, the background share-image decode, and the HotKnot
// session that exchanges the dokan descriptor. Subclasses pick the role and react
// through the on*() hooks (all on the LVGL thread).
class TransferScreen : public NavigationScreen {
public:
    explicit TransferScreen(std::shared_ptr<SharedCardData> data);
    ~TransferScreen() override;
    void back() override;

protected:
    static constexpr int kShareImageW = 203;
    static constexpr int kShareImageH = 360;

    // Build the share-image row under `parent` and start decoding data_'s share
    // images into it (no-op when there are none). Returns the row container.
    lv_obj_t *createShareImages(lv_obj_t *parent);

    // Lazily built name font (Montserrat -> NotoSansJP -> PDF glyph supplement).
    const lv_font_t *nameFont();

    // HotKnot descriptor exchange. Approach for `role`; startHotKnot returns false
    // where HotKnot is unavailable (e.g. the simulator).
    bool startHotKnot(bsp_hotknot_role_t role);
    void endHotKnot();
    void sendHotKnot(const void *data, size_t len);  // caller keeps `data` alive
    void failHotKnot(esp_err_t err);

    virtual void onHotKnotPaired();             // default: open the progress modal
    virtual void onHotKnotReady() {}            // ready to send (master)
    virtual void onHotKnotDone() {}             // sent (master) / received (slave)
    virtual void onHotKnotFailed(esp_err_t err);
    virtual void stashReceived(const uint8_t *data, size_t len) {}  // on the reader task
    virtual const char *transferTitle() const { return "Transfer"; }

    void showProgressModal(const char *message);
    void setProgressMessage(const char *message);
    void addModalCloseButton(const char *text = "Close");

    std::shared_ptr<SharedCardData> data_;

private:
    // One decoded share image. The worker fills img/dsc then publishes state with
    // release; the poll timer reads state with acquire before touching img/dsc.
    // Held by unique_ptr so dsc keeps a stable address for lv_image_set_src.
    struct Slot {
        imgproc::Image   img;
        lv_image_dsc_t   dsc{};
        lv_obj_t        *obj = nullptr;  // placeholder created up front; src set when ready
        std::atomic<int> state{0};       // 0 pending, 1 ready, 2 failed
        bool             shown = false;
    };

    void startWorker();
    void stopWorker();  // request stop, then wait for the worker to exit
    void worker();      // worker-task body: decode each share image in order
    static void workerTask(void *arg);
    void poll();        // LVGL-context: fill each newly ready slot's lv_image

    std::unique_ptr<NameFont> name_font_;
    lv_obj_t *share_images_ = nullptr;
    lv_timer_t *poll_timer_ = nullptr;

    std::vector<std::unique_ptr<Slot>> slots_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> worker_running_{false};
    bool worker_started_ = false;

    // hotKnotEvent (reader task) and sendTask publish hk_state_ with release;
    // pollHotKnot (LVGL thread) replays the transitions with acquire.
    enum class HkState : uint8_t { Idle, Approaching, Paired, Ready, Done, Failed };
    static void hotKnotEvent(const bsp_hotknot_event_t *ev, void *arg);
    void pollHotKnot();
    static void sendTask(void *arg);

    lv_obj_t *modal_ = nullptr;
    lv_obj_t *modal_message_ = nullptr;
    lv_timer_t *hk_timer_ = nullptr;
    bool hk_active_ = false;

    std::atomic<HkState> hk_state_{HkState::Idle};
    std::atomic<bool> hk_send_busy_{false};
    HkState hk_seen_ = HkState::Idle;
    esp_err_t hk_err_ = ESP_OK;
    const void *hk_send_data_ = nullptr;
    size_t hk_send_len_ = 0;
};
