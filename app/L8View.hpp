/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <cstdint>

// A non-owning view of an L8 (8bpp grayscale, high-nibble = EPD gray) image
// buffer: a decoded imgproc::Image, or a blob mapped straight from the My Card
// flash partition. Dependency-free (no LVGL, no image_processor) so it can sit
// between the storage/decode layers and the LVGL adapter. The pointed-to bytes
// must outlive the view.
struct L8View {
    const uint8_t *data = nullptr;
    uint16_t w = 0;
    uint16_t h = 0;
    uint32_t stride = 0;
    uint8_t  levels = 16;

    bool valid() const { return data != nullptr; }
};
