/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "NameFont.hpp"
#include "namecard_pdf.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A card's display name ready to draw as an lv_label: the UTF-8 text plus a
// NameFont at a chosen size (Montserrat -> NotoSansJP -> the card's rare-kanji
// glyph supplement). Feed it a parsed card — NameCardData and SharedCardData
// both expose name()/name_glyphs() — or load the My Card straight from flash.
// Owns whatever the font's GlyphSet references, so it must outlive any label
// built from make_label()/font().
class CardNameLabel {
public:
    // From an already-parsed card. `glyphs` (may be null) and its backing blob
    // are referenced, not copied, so they must outlive this. False for no name.
    bool set(const std::string &name, const nckpdf::GlyphSet *glyphs, uint16_t px);

    // Read the My Card name from the mycard flash PDF, copying the glyph
    // supplement so nothing external need outlive this.
    bool load_mycard(uint16_t px);

    void reset();
    bool valid() const { return font_ && !text_.empty(); }
    const std::string &text() const { return text_; }
    const lv_font_t *font() const { return font_ ? font_->font() : nullptr; }

    // A centered, ellipsized full-width label under `parent` (nullptr if
    // !valid()); the caller may restyle the returned object.
    lv_obj_t *make_label(lv_obj_t *parent) const;

private:
    std::string text_;
    std::vector<uint8_t> glyph_blob_;  // load_mycard: owns the supplement bytes
    nckpdf::GlyphSet glyphs_;          // load_mycard: parsed from glyph_blob_
    std::unique_ptr<NameFont> font_;
};
