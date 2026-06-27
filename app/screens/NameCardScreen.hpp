/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "NameCardData.hpp"
#include "NameFont.hpp"
#include <memory>

class NameCardScreen : public Screen {
public:
    explicit NameCardScreen(std::shared_ptr<NameCardData> data);
    void build() override;
    void onAppear() override;

private:
    std::shared_ptr<NameCardData> data_;  // owns the decoded image + metadata
    lv_image_dsc_t dsc_{};   // references the display image's buffer (kept alive by data_)

    // The name label's font chain, built lazily and cached (its mutable font
    // copies must outlive any label using them).
    std::unique_ptr<NameFont> name_font_;
    const lv_font_t *nameFont();

    void openMenu();
    void openInfo();
};
