/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "NameCardScreen.hpp"
#include "NameCardKnot.hpp"

NameCardScreen::NameCardScreen(std::string path) : path_(std::move(path)) {}

void NameCardScreen::build() {
    auto image = lv_image_create(root_);
    std::string src = "A:" + path_;  // LVGL STDIO fs driver letter (see lv_conf.h)
    lv_image_set_src(image, src.c_str());
    lv_obj_center(image);
}

void NameCardScreen::onAppear() {
    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY_FULL);
}
