/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"

class HomeScreen : public Screen {
public:
    void build() override;
    void onAppear() override;
    void onDisappear() override;

private:
    // The My Card area is rebuilt from MyCardStore on every appearance: its
    // preview/name lv_images point straight into the mmap'd flash, which an
    // import re-maps, so the images must be dropped while away and re-created on
    // return. onDisappear() cleans them so no stale MMIO pointer survives.
    lv_obj_t *mycard_section_ = nullptr;
    lv_image_dsc_t preview_dsc_{};  // -> BLOB_PREVIEW (mmap)
    lv_image_dsc_t name_dsc_{};     // -> BLOB_NAME (mmap)

    void refreshMyCard();
    void importMyCard();
    void myCardButtonCreate(lv_obj_t *parent);
    void noCardButtonCreate(lv_obj_t *parent);
};
