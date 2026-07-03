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
    // Back: pushed from the browser, bottom-left pops back to it.
    // Home: loaded from Home / boot resume, bottom-left loads HomeScreen.
    enum class Nav { Back, Home };

    NameCardScreen(std::shared_ptr<NameCardData> data, Nav nav);
    ~NameCardScreen() override;
    void build() override;
    void onAppear() override;

private:
    std::shared_ptr<NameCardData> data_;  // owns the decoded image + metadata
    Nav nav_;
    lv_image_dsc_t dsc_{};   // references the display image's buffer (kept alive by data_)
    lv_timer_t *poll_ = nullptr;  // boot resume: data_ may still be decoding

    // The name label's font chain, built lazily and cached (its mutable font
    // copies must outlive any label using them).
    std::unique_ptr<NameFont> name_font_;
    const lv_font_t *nameFont();

    void showImage();
    void poll();
    void leave();
    void openMenu();
    void openInfo();
};
