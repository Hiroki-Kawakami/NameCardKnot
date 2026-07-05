/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "UiFont.hpp"
#include "resources.h"

namespace {

lv_font_t s_font24, s_font32, s_font48;

// copy: built-in fonts are const. Noto's descender (base_line) runs deeper
// than Montserrat's at the same ascent, so raising both line_height and
// base_line to Noto's keeps the shared baseline while making room for it.
lv_font_t build_chain(const lv_font_t &mont, const lv_font_t *noto) {
    lv_font_t f = mont;
    f.fallback = noto;
    if (noto->line_height > f.line_height) {
        f.line_height = noto->line_height;
        f.base_line = noto->base_line;
    }
    return f;
}

}  // namespace

void ui_font_init() {
    s_font24 = build_chain(lv_font_montserrat_24, R.font.noto_sans_jp_24);
    s_font32 = build_chain(lv_font_montserrat_32, R.font.noto_sans_jp_32);
    s_font48 = build_chain(lv_font_montserrat_48, R.font.noto_sans_jp_48);
}

const lv_font_t *ui_font_24() { return &s_font24; }
const lv_font_t *ui_font_32() { return &s_font32; }
const lv_font_t *ui_font_48() { return &s_font48; }
