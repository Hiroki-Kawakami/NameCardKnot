/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "namecard_pdf.hpp"
#include <cstdint>
#include <vector>

// Rasterize the card name to a small grayscale image for the Home screen, so the
// home view needs no fonts. The text is drawn with the NameFont chain at 48px
// (where the embedded 1bpp glyph supplement is defined) and box-downscaled to a
// 32px-tall L8 image — the supersampling turns the 1bpp rare-kanji glyphs into
// smooth antialiased gray.
//
// Uses the LVGL canvas/draw pipeline, so it MUST be called with the LVGL lock
// held (lv_lock()/lv_unlock()). LVGL-bound (unlike the rest of the importer),
// which is why it lives behind its own header.
struct NameRaster {
    std::vector<uint8_t> data;  // L8, tightly packed (stride == w), byte = level*17
    uint16_t w = 0;
    uint16_t h = 0;
    uint32_t stride = 0;
    uint8_t  levels = 16;

    bool valid() const { return !data.empty(); }
};

NameRaster render_name_l8(const char *text, const nckpdf::GlyphSet *glyphs,
                          uint16_t max_src_w = 1200);
