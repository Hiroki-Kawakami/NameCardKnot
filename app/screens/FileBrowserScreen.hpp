/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "widgets.hpp"
#include "image_processor.hpp"
#include "FileLoader.hpp"
#include <functional>
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

    // Async file load shown in a modal. The loader (a FileLoader) reports
    // progress/cancel; onLoaded_ does the per-type transition. The modal + poll
    // loop here are common across file types.
    lv_obj_t *card_ = nullptr;
    lv_obj_t *bar_ = nullptr;
    lv_timer_t *poll_ = nullptr;
    std::shared_ptr<FileLoader> loader_;
    std::function<void()> onLoaded_;
    bool cancelling_ = false;
    int last_pct_ = 0;
    uint32_t last_tick_ = 0;
    void openProgress();
    void poll();
    void stopLoad();
};
