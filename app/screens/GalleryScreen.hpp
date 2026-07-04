/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "widgets.hpp"
#include "CardNameLabel.hpp"
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// Lists received cards (RECEIVED_CARDS_DIR, *.snc.pdf) newest-first by mtime.
class GalleryScreen : public NavigationScreen {
public:
    void build() override;
    void back() override;

private:
    struct Entry {
        std::string name;
        time_t mtime;
    };
    std::vector<Entry> entries_;
    std::string error_;
    size_t offset_ = 0;
    // Keeps each shown row's NameFont (and glyph blob) alive for the label it
    // built; refilled every rebuild().
    std::vector<std::unique_ptr<CardNameLabel>> row_names_;

    void load();
    void rebuild();
    void open(int index);
};
