/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "widgets.hpp"
#include <string>

class FileBrowserScreen : public NavigationScreen {
public:
    explicit FileBrowserScreen(std::string path = "/sdcard");
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
};
