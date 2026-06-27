/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "lvgl.hpp"
#include "lv_glyph_font.hpp"
#include "namecard_pdf.hpp"
#include "resources.h"
#include <memory>

// The name label font as a fallback chain: Montserrat (built-in) -> NotoSansJP
// (built-in) -> the PDF's embedded glyph supplement (when present). Owns the
// mutable font copies (built-in fonts are const) and the GlyphFont, so they
// outlive any label/render that uses font(). The GlyphSet passed in — and the
// blob its RLE pointers reference — must outlive this. Shared by NameCardScreen
// (label) and the My Card import (offscreen name raster).
class NameFont {
public:
    explicit NameFont(const nckpdf::GlyphSet *glyphs) {
        mont_ = lv_font_montserrat_48;   // copy: built-in fonts are const
        noto_ = *R.font.noto_sans_jp_48;
        const lv_font_t *tail = nullptr;
        if (glyphs) {
            glyph_font_ = std::make_unique<GlyphFont>(*glyphs);
            tail = glyph_font_->font();
        }
        noto_.fallback = tail;
        mont_.fallback = &noto_;
    }

    const lv_font_t *font() const { return &mont_; }

private:
    lv_font_t mont_{};
    lv_font_t noto_{};
    std::unique_ptr<GlyphFont> glyph_font_;
};
