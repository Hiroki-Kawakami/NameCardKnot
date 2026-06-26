/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "NameCardData.hpp"
#include <memory>

class NameCardScreen : public Screen {
public:
    explicit NameCardScreen(std::shared_ptr<NameCardData> data);
    void build() override;
    void onAppear() override;

private:
    std::shared_ptr<NameCardData> data_;  // owns the decoded image + metadata
    lv_image_dsc_t dsc_{};   // references the display image's buffer (kept alive by data_)

    void openMenu();
};
