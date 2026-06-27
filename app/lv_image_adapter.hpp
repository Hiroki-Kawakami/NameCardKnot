/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "lvgl.hpp"
#include "image_processor.hpp"
#include "L8View.hpp"

// Point an lv_image_dsc_t at an L8 buffer (no copy). The buffer must outlive the
// descriptor and the lv_image using it. L8 maps 1:1 onto the EPD (high nibble =
// gray) and needs no palette.
inline bool l8view_fill_lv_dsc(const L8View &v, lv_image_dsc_t &dsc) {
    if (!v.data) return false;
    dsc = lv_image_dsc_t{};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_L8;
    dsc.header.w = v.w;
    dsc.header.h = v.h;
    dsc.header.stride = v.stride;
    dsc.data = v.data;
    dsc.data_size = v.stride * v.h;
    return true;
}

// Wraps a decoded imgproc::Image into an lv_image_dsc_t that references the
// Image's buffer (no copy). The Image must outlive the descriptor and the
// lv_image using it. Keeps image_processor itself LVGL-free — this binding lives
// in the app, like the BSP<->LVGL flush binding.
//
// L8 only for now: it maps 1:1 onto the EPD (high nibble = gray) and needs no
// palette. I4/I1 would require prepending an LVGL palette to the data buffer.
inline bool imgproc_fill_lv_dsc(const imgproc::Image &img, lv_image_dsc_t &dsc) {
    if (!img.data || img.format != imgproc::OutFormat::L8) return false;
    return l8view_fill_lv_dsc(
        L8View{img.data, img.w, img.h, static_cast<uint32_t>(img.stride), img.levels}, dsc);
}
