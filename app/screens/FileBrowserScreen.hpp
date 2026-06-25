/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "widgets.hpp"
#include "image_processor.hpp"
#include <memory>
#include <string>

class FileBrowserScreen : public NavigationScreen {
public:
    explicit FileBrowserScreen(std::string path = "/sdcard");
    ~FileBrowserScreen() override;
    void build() override;
    void back() override;

private:
    struct Entry {
        std::string name;
        bool dir;
    };

    std::vector<std::string> path_stack_;
    std::vector<Entry> entries_;
    std::string error_;
    size_t offset_;

    void load();
    void rebuild();
    void open(int index);

    // Async image load shown in a modal (decode runs off the UI task).
    lv_obj_t *card_ = nullptr;
    lv_obj_t *bar_ = nullptr;
    lv_timer_t *poll_ = nullptr;
    std::shared_ptr<imgproc::DecodeJob> job_;
    bool cancelling_ = false;
    int last_pct_ = 0;
    int step_pct_ = 5;          // adaptive update granularity (5/10/20/50%)
    uint32_t last_tick_ = 0;
    void openProgress(const std::string &name, const std::string &path);
    void poll();
    void stopLoad();
};
