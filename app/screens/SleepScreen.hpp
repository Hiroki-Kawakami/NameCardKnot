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

// What the glass shows while powered off (non-card screens only; sleeping from
// the card keeps the card). Tapping it recovers to Home — reachable only when
// USB power kept the board alive through bsp_power_off.
class SleepScreen : public Screen {
public:
    void build() override;

private:
    // RAM copy of BLOB_PREVIEW, not a flash mmap: two mmaps of the same partition
    // can share a 64KB MMU page, so releasing one invalidates the other.
    std::vector<uint8_t> preview_buf_;  // preview_dsc_ points in
    lv_image_dsc_t preview_dsc_{};
    CardNameLabel name_;  // outlives the name label it builds

    bool readPreview();  // load BLOB_PREVIEW into preview_buf_ + preview_dsc_
};
