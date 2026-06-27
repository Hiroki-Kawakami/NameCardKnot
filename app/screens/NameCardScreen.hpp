/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "screen_manager.hpp"
#include "lvgl.hpp"
#include "NameCardData.hpp"
#include "lv_glyph_font.hpp"
#include <memory>

class NameCardScreen : public Screen {
public:
    explicit NameCardScreen(std::shared_ptr<NameCardData> data);
    void build() override;
    void onAppear() override;

private:
    std::shared_ptr<NameCardData> data_;  // owns the decoded image + metadata
    lv_image_dsc_t dsc_{};   // references the display image's buffer (kept alive by data_)

    // The name label's font: Montserrat (built-in) -> NotoSansJP (built-in) ->
    // the PDF's embedded glyph supplement (when present). Built lazily and cached
    // because the chain's mutable copies must outlive any label using them.
    lv_font_t mont_{};
    lv_font_t noto_{};
    std::unique_ptr<GlyphFont> glyph_font_;
    const lv_font_t *name_font_ = nullptr;
    const lv_font_t *nameFont();

    void openMenu();
    void openInfo();
};
