/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "CardNameLabel.hpp"
#include "CardStore.hpp"

void CardNameLabel::reset() {
    text_.clear();
    glyph_blob_.clear();
    glyphs_ = {};
    font_.reset();
}

bool CardNameLabel::set(const std::string &name, const nckpdf::GlyphSet *glyphs, uint16_t px) {
    reset();
    if (name.empty()) return false;
    text_ = name;
    font_ = std::make_unique<NameFont>(glyphs, px);
    return true;
}

bool CardNameLabel::load_mycard(uint16_t px) {
    reset();
    auto &st = cardstore::mycard();
    uint32_t len = st.blob_len(cardstore::BLOB_PDF);
    if (!len) return false;
    std::vector<uint8_t> pdf(len);
    nckpdf::Card card;
    if (!st.read_blob(cardstore::BLOB_PDF, pdf.data(), len) ||
        nckpdf::parse_buffer(pdf.data(), len, card) != nckpdf::Status::Ok ||
        card.name.empty())
        return false;
    text_ = card.name;

    // Keep only the glyph asset bytes (the font's GlyphSet points into them); the
    // PDF copy is transient.
    const nckpdf::GlyphSet *glyphs = nullptr;
    const nckpdf::Asset *a = card.find(nckpdf::AssetType::NameGlyphs);
    if (a && a->length && uint64_t(a->offset) + a->length <= pdf.size()) {
        glyph_blob_.assign(pdf.begin() + a->offset, pdf.begin() + a->offset + a->length);
        if (nckpdf::parse_name_glyphs(glyph_blob_.data(), glyph_blob_.size(), glyphs_) == nckpdf::Status::Ok)
            glyphs = &glyphs_;
        else
            glyph_blob_.clear();
    }
    font_ = std::make_unique<NameFont>(glyphs, px);
    return true;
}

lv_obj_t *CardNameLabel::make_label(lv_obj_t *parent) const {
    if (!valid()) return nullptr;
    auto label = lv_label_create(parent);
    lv_label_set_text(label, text_.c_str());
    lv_obj_set_style_text_font(label, font_->font(), 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}
