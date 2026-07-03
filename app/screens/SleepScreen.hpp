/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "CardStore.hpp"

// What the glass shows while powered off (non-card screens only; sleeping from
// the card keeps the card). Tapping it recovers to Home — reachable only when
// USB power kept the board alive through bsp_power_off.
class SleepScreen : public Screen {
public:
    void build() override;

private:
    cardstore::MappedImage preview_map_, name_map_;
    lv_image_dsc_t preview_dsc_{}, name_dsc_{};
};
