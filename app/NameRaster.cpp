/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "NameRaster.hpp"
#include "NameFont.hpp"
#include "lvgl.hpp"

namespace {
constexpr int kRenderPx = 48;  // glyph supplement is defined at 48px
constexpr int kOutH = 32;      // home name height
}  // namespace

NameRaster render_name_l8(const char *text, const nckpdf::GlyphSet *glyphs, uint16_t max_src_w) {
    NameRaster out;
    if (!text || !text[0]) return out;

    NameFont nf(glyphs);
    const lv_font_t *font = nf.font();

    lv_point_t sz;
    lv_text_get_size(&sz, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    int sw = sz.x, sh = sz.y;
    if (sw <= 0 || sh <= 0) return out;
    if (sw > max_src_w) sw = max_src_w;  // clamp pathologically long names

    // Render black text on white into an offscreen L8 canvas (a throwaway,
    // never-loaded screen, so nothing reaches the display).
    lv_draw_buf_t *db = lv_draw_buf_create(sw, sh, LV_COLOR_FORMAT_L8, 0);
    if (!db) return out;
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *canvas = lv_canvas_create(scr);
    lv_canvas_set_draw_buf(canvas, db);
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font;
    dsc.color = lv_color_black();
    dsc.align = LV_TEXT_ALIGN_LEFT;
    lv_area_t coords = {0, 0, sw - 1, sh - 1};
    lv_draw_label(&layer, &dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);

    const uint8_t *src = static_cast<const uint8_t *>(db->data);
    const uint32_t src_stride = db->header.stride;

    // Box-downscale to kOutH tall, preserving aspect, quantizing to 16 EPD levels
    // (byte = level*17, matching the display/preview caches).
    int ow = (int)((int64_t)sw * kOutH / sh);
    if (ow < 1) ow = 1;
    out.w = ow;
    out.h = kOutH;
    out.stride = ow;
    out.levels = 16;
    out.data.resize((size_t)ow * kOutH);

    for (int oy = 0; oy < kOutH; oy++) {
        int sy0 = oy * sh / kOutH, sy1 = (oy + 1) * sh / kOutH;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int ox = 0; ox < ow; ox++) {
            int sx0 = ox * sw / ow, sx1 = (ox + 1) * sw / ow;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t sum = 0, cnt = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) { sum += src[(size_t)sy * src_stride + sx]; cnt++; }
            uint32_t avg = cnt ? sum / cnt : 255;
            uint32_t level = (avg * 15 + 127) / 255;   // 0..15
            out.data[(size_t)oy * ow + ox] = (uint8_t)(level * 17);
        }
    }

    lv_obj_delete(scr);          // deletes the canvas; the draw buf is ours to free
    lv_draw_buf_destroy(db);
    return out;
}
