/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "image_processor.hpp"
#include "lvgl.hpp"

class NameCardScreen : public Screen {
public:
    explicit NameCardScreen(imgproc::Image image);
    void build() override;
    void onAppear() override;

private:
    imgproc::Image image_;   // owns the decoded buffer (decoded by FileBrowserScreen)
    lv_image_dsc_t dsc_{};   // references image_'s buffer (must outlive the lv_image)

    void openMenu();
};
