/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "CardStore.hpp"

class HomeScreen : public Screen {
public:
    void build() override;
    void onAppear() override;
    void onDisappear() override;

private:
    // The My Card area is rebuilt from the mycard store on every appearance: its
    // preview/name lv_images point into flash mapped on demand. The mappings are
    // dropped while away (so an import can rewrite the partition) and re-acquired
    // on return; onDisappear() releases them.
    lv_obj_t *mycard_section_ = nullptr;
    cardstore::MappedImage preview_map_;  // BLOB_PREVIEW mapping (preview_dsc_ points in)
    cardstore::MappedImage name_map_;     // BLOB_NAME mapping (name_dsc_ points in)
    lv_image_dsc_t preview_dsc_{};
    lv_image_dsc_t name_dsc_{};

    void refreshMyCard();
    void importMyCard();
    void myCardButtonCreate(lv_obj_t *parent);
    void noCardButtonCreate(lv_obj_t *parent);
};
