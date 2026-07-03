/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "lvgl.hpp"
#include "namecard_pdf.hpp"
#include <algorithm>
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
// hands us. `target_px` (0 = native 48px) scales the glyphs to a smaller
// built-in size (only 24/32 are used), area-averaging the source 1bpp into
// antialiased A8 with pure integer math (scale = target_px/glyph_px = num/den).
class GlyphFont {
public:
    explicit GlyphFont(const nckpdf::GlyphSet &gs, uint16_t target_px = 0) : gs_(gs) {
        if (target_px && gs.glyph_px && target_px != gs.glyph_px) {
            num_ = target_px;
            den_ = gs.glyph_px;
        }
        size_t max_bits = 0;
        for (const auto &g : gs.glyphs) {
            size_t bits = size_t(g.box_w) * g.box_h;
            if (bits > max_bits) max_bits = bits;
        }
        scratch_.resize((max_bits + 7) / 8);

        font_.get_glyph_dsc = get_glyph_dsc_cb;
        font_.get_glyph_bitmap = get_glyph_bitmap_cb;
        font_.line_height = scaled(gs.line_height);
        // The blob's base_line is measured from the TOP (docs §4.4); LVGL's
        // base_line is from the bottom of line_height. Convert so embedded
        // glyphs share the built-in font's baseline.
        font_.base_line = scaled(gs.line_height - gs.base_line);
        font_.dsc = this;
    }

    const lv_font_t *font() const { return &font_; }

private:
    static bool get_glyph_dsc_cb(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                                 uint32_t letter, uint32_t /*next*/) {
        auto *self = static_cast<const GlyphFont *>(font->dsc);
        const nckpdf::Glyph *g = self->find(letter);
        if (!g) return false;  // not in this supplement -> let LVGL try the fallback
        // blob adv_w is 1/16 px (8.4 fixed): round(adv_w*num/den / 16) to px.
        out->adv_w = (long(g->adv_w) * self->num_ + 8 * self->den_) / (16 * self->den_);
        out->box_w = g->box_w ? std::max(1, self->scaled(g->box_w)) : 0;
        out->box_h = g->box_h ? std::max(1, self->scaled(g->box_h)) : 0;
        out->ofs_x = self->scaled(g->ofs_x);
        out->ofs_y = self->scaled(g->ofs_y);
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

        // 1bpp source (row-major, MSB-first, no row padding) -> A8. When the dsc
        // box was scaled down, area-average each destination cell over its source
        // footprint (identity when box_w/box_h are unscaled -> 0x00 / 0xFF).
        // Overlaps are measured in units of 1/dw (x) and 1/dh (y) source pixels,
        // so the whole cross-product stays integer; the cell's total weight is
        // sw*sh regardless of scale.
        const uint8_t *bits = self->scratch_.data();
        auto src_on = [&](uint32_t x, uint32_t y) -> bool {
            size_t idx = size_t(y) * g.box_w + x;
            return (bits[idx >> 3] >> (7 - (idx & 7))) & 1;
        };
        uint8_t *dst = draw_buf->data;
        const uint32_t stride = draw_buf->header.stride;
        const uint32_t sw = g.box_w, sh = g.box_h;
        const uint32_t dw = g_dsc->box_w, dh = g_dsc->box_h;
        const uint32_t area = sw * sh;
        for (uint32_t dy = 0; dy < dh; dy++) {
            uint8_t *row = dst + size_t(dy) * stride;
            const uint32_t ry0 = dy * sh, ry1 = ry0 + sh;
            for (uint32_t dx = 0; dx < dw; dx++) {
                const uint32_t rx0 = dx * sw, rx1 = rx0 + sw;
                uint32_t cover = 0;
                for (uint32_t sy = ry0 / dh; sy * dh < ry1; sy++) {
                    const uint32_t wy = std::min((sy + 1) * dh, ry1) - std::max(sy * dh, ry0);
                    for (uint32_t sx = rx0 / dw; sx * dw < rx1; sx++) {
                        if (!src_on(sx, sy)) continue;
                        cover += wy * (std::min((sx + 1) * dw, rx1) - std::max(sx * dw, rx0));
                    }
                }
                row[dx] = uint8_t((cover * 255 + area / 2) / area);
            }
        }
        return draw_buf;
    }

    // round(v * num_/den_) with half away from zero; identity when num_==den_==1.
    int scaled(int v) const {
        long n = long(v) * num_;
        return n >= 0 ? (n * 2 + den_) / (den_ * 2) : -((-n * 2 + den_) / (den_ * 2));
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
    int num_ = 1, den_ = 1;  // scale = num_/den_ (target_px : glyph_px)
    lv_font_t font_{};
    mutable std::vector<uint8_t> scratch_;  // largest glyph's 1bpp bits, reused
};
