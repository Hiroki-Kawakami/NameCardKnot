/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "SharedCardData.hpp"
#include "CardNameLabel.hpp"
#include "NameFont.hpp"
#include "image_processor.hpp"
#include <memory>

// Viewer for a received share-only card (.snc.pdf): name, share image(s), and
// URL/Message/image-toggle buttons as the card provides them.
class SharedCardScreen : public Screen {
public:
    // Back: pushed from GalleryScreen, pops back to it.
    // Received: loaded straight onto the stack after a boot-time receive; the
    // button resumes the lastcard NameCardScreen if one is still recorded, else Home.
    enum class Nav { Back, Received };

    SharedCardScreen(std::shared_ptr<SharedCardData> data, Nav nav);
    void build() override;
    void onAppear() override;

private:
    std::shared_ptr<SharedCardData> data_;
    Nav nav_;

    CardNameLabel name_;
    imgproc::Image images_[2];
    lv_image_dsc_t dsc_[2]{};
    bool decoded_[2] = {false, false};
    int shown_image_ = 0;
    lv_obj_t *image_area_ = nullptr;
    lv_obj_t *image_obj_ = nullptr;
    lv_obj_t *image_toggle_label_ = nullptr;

    // Message text font: Montserrat -> NotoSansJP (no glyph supplement — that
    // covers the name's rare kanji, not free-form message text).
    std::unique_ptr<NameFont> message_font_;
    const lv_font_t *messageFont();

    void buildButtonBar();
    bool decodeImage(int index);
    void showImage(int index);
    void toggleImage();
    void openUrlModal();
    void openMessageModal();
};
