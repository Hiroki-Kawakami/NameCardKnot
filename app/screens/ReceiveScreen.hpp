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

class ReceiveScreen : public NavigationScreen {
public:
    explicit ReceiveScreen(std::shared_ptr<SharedCardData> data);
    ~ReceiveScreen() override;
    void build() override;
    void onAppear() override;
    void onDisappear() override;
    void back() override;

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

    std::shared_ptr<SharedCardData> data_;
    std::unique_ptr<NameFont> name_font_;
    lv_obj_t *share_images_ = nullptr;
    lv_timer_t *poll_timer_ = nullptr;

    std::vector<std::unique_ptr<Slot>> slots_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> worker_running_{false};
    bool worker_started_ = false;

    bool return_my_data_ = false;

    void loadShareCardData();
};
