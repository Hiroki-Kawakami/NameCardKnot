/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "CardStore.hpp"
#include "CardNameLabel.hpp"

class HomeScreen : public Screen {
public:
    void build() override;
    void onAppear() override;
    void onDisappear() override;

private:
    // The My Card area is rebuilt from the mycard store on every appearance: the
    // preview lv_image points into flash mapped on demand, the name is a live
    // lv_label drawn with the NameFont chain. The mapping is dropped while away
    // (so an import can rewrite the partition) and re-acquired on return;
    // onDisappear() releases it and the name font.
    lv_obj_t *mycard_section_ = nullptr;
    cardstore::MappedImage preview_map_;  // BLOB_PREVIEW mapping (preview_dsc_ points in)
    lv_image_dsc_t preview_dsc_{};
    CardNameLabel name_;  // outlives the name label it builds

    void refreshMyCard();
    void importMyCard();
    void myCardButtonCreate(lv_obj_t *parent);
    void noCardButtonCreate(lv_obj_t *parent);
};
