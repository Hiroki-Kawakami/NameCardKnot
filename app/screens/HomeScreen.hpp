/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "CardStore.hpp"
#include "CardNameLabel.hpp"
#include <cstdint>
#include <vector>

class HomeScreen : public Screen {
public:
    void build() override;
    void onAppear() override;
    void onDisappear() override;

private:
    // The My Card area is rebuilt from the mycard store on every appearance: the
    // preview lv_image points at a RAM copy of BLOB_PREVIEW (not a flash mmap —
    // two mmaps of the same partition can share a 64KB MMU page, so releasing one
    // invalidates the other), the name is a live lv_label drawn with the NameFont
    // chain. onDisappear() drops the copy and the name font.
    lv_obj_t *mycard_section_ = nullptr;
    std::vector<uint8_t> preview_buf_;  // BLOB_PREVIEW bytes (preview_dsc_ points in)
    lv_image_dsc_t preview_dsc_{};
    CardNameLabel name_;  // outlives the name label it builds
    lv_obj_t *date_label_ = nullptr;
    lv_obj_t *battery_image_ = nullptr;

    void refreshMyCard();
    void refreshDate();
    void refreshBattery();
    void importMyCard();
    void myCardButtonCreate(lv_obj_t *parent);
    void noCardButtonCreate(lv_obj_t *parent);
    bool readPreview();  // load BLOB_PREVIEW into preview_buf_ + preview_dsc_
};
