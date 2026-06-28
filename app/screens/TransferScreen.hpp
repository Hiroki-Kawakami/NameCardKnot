/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "widgets.hpp"
#include "SharedCardData.hpp"
#include "NameFont.hpp"
#include "image_processor.hpp"
#include <atomic>
#include <memory>
#include <vector>

// Base for the card-transfer screens (ShareScreen / ReceiveScreen). HotKnot and
// dokan only differ in Master/Slave (AP/STA) role, so the two screens share most
// of their behavior; this holds the common parts. For now that is the peer card
// (data_), its name font, and the background decode of its share images onto the
// row built by createShareImages(). The HotKnot/dokan session will live here too.
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
};
