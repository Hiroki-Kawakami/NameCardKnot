/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "lvgl.hpp"

// App-global UI font chains: Montserrat (built-in) -> NotoSansJP (built-in),
// one per size used by the theme default / widget injection. ui_font_init()
// must run once before any of the accessors are used.
void ui_font_init();
const lv_font_t *ui_font_24();
const lv_font_t *ui_font_32();
const lv_font_t *ui_font_48();
