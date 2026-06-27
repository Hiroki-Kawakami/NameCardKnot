/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "lvgl.hpp"
#include "namecard_pdf.hpp"
#include <cstdint>
#include <vector>

// Wraps a parsed name_glyphs GlyphSet (the rare-kanji supplement embedded in a
// .mnc.pdf) into a runtime lv_font_t, so it can hang off a built-in font's
// `fallback` chain. The GlyphSet — and the blob its glyph RLE pointers
// reference — must outlive this. Keeps namecard_pdf LVGL-free: this binding
// lives in the app, like lv_image_adapter.hpp.
//
// Glyphs are 1bpp (§4.4 of docs/namecard_pdf.md); we expand them to the A8 mask
// LVGL's software label renderer expects, per glyph, into the draw buffer LVGL
// hands us.
class GlyphFont {
public:
    explicit GlyphFont(const nckpdf::GlyphSet &gs) : gs_(gs) {
        size_t max_bits = 0;
        for (const auto &g : gs.glyphs) {
            size_t bits = size_t(g.box_w) * g.box_h;
            if (bits > max_bits) max_bits = bits;
        }
        scratch_.resize((max_bits + 7) / 8);

        font_.get_glyph_dsc = get_glyph_dsc_cb;
        font_.get_glyph_bitmap = get_glyph_bitmap_cb;
        font_.line_height = gs.line_height;
        // The blob's base_line is measured from the TOP (docs §4.4); LVGL's
        // base_line is from the bottom of line_height. Convert so embedded
        // glyphs share the built-in font's baseline.
        font_.base_line = gs.line_height - gs.base_line;
        font_.dsc = this;
    }

    const lv_font_t *font() const { return &font_; }

private:
    static bool get_glyph_dsc_cb(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                                 uint32_t letter, uint32_t /*next*/) {
        auto *self = static_cast<const GlyphFont *>(font->dsc);
        const nckpdf::Glyph *g = self->find(letter);
        if (!g) return false;  // not in this supplement -> let LVGL try the fallback
        out->adv_w = (g->adv_w + 8) >> 4;  // blob is 1/16 px (8.4 fixed); LVGL dsc adv_w is px
        out->box_w = g->box_w;
        out->box_h = g->box_h;
        out->ofs_x = g->ofs_x;
        out->ofs_y = g->ofs_y;
        out->format = LV_FONT_GLYPH_FORMAT_A8;
        out->is_placeholder = false;
        out->gid.index = uint32_t(g - self->gs_.glyphs.data()) + 1;
        return true;
    }

    static const void *get_glyph_bitmap_cb(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf) {
        auto *self = static_cast<const GlyphFont *>(g_dsc->resolved_font->dsc);
        if (g_dsc->gid.index == 0) return nullptr;
        const nckpdf::Glyph &g = self->gs_.glyphs[g_dsc->gid.index - 1];
        const size_t nbits = size_t(g.box_w) * g.box_h;
        if (nbits == 0) return nullptr;

        nckpdf::rle_decode(g.rle, g.rle_len, self->scratch_.data(), nbits);

        // 1bpp (row-major, MSB-first, no row padding) -> A8 (0x00 / 0xFF).
        const uint8_t *bits = self->scratch_.data();
        uint8_t *dst = draw_buf->data;
        const uint32_t stride = draw_buf->header.stride;
        size_t idx = 0;
        for (uint32_t y = 0; y < g.box_h; y++) {
            uint8_t *row = dst + size_t(y) * stride;
            for (uint32_t x = 0; x < g.box_w; x++, idx++) {
                bool on = (bits[idx >> 3] >> (7 - (idx & 7))) & 1;
                row[x] = on ? 0xFF : 0x00;
            }
        }
        return draw_buf;
    }

    // Glyphs are sorted by codepoint (§4.4) -> binary search.
    const nckpdf::Glyph *find(uint32_t cp) const {
        const auto &v = gs_.glyphs;
        size_t lo = 0, hi = v.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (v[mid].codepoint < cp) lo = mid + 1;
            else hi = mid;
        }
        return (lo < v.size() && v[lo].codepoint == cp) ? &v[lo] : nullptr;
    }

    const nckpdf::GlyphSet &gs_;
    lv_font_t font_{};
    mutable std::vector<uint8_t> scratch_;  // largest glyph's 1bpp bits, reused
};
