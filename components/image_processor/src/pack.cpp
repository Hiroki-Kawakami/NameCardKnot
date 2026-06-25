/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "pack.hpp"

#include <cstring>

namespace imgproc {

size_t out_stride(OutFormat fmt, int width) {
    switch (fmt) {
        case OutFormat::I4: return static_cast<size_t>((width + 1) / 2);
        case OutFormat::I1: return static_cast<size_t>((width + 7) / 8);
        case OutFormat::L8: default: return static_cast<size_t>(width);
    }
}

void pack_row(OutFormat fmt, const uint8_t *levels, int width, int n, uint8_t *dst) {
    int denom = n > 1 ? n - 1 : 1;
    switch (fmt) {
        case OutFormat::L8:
            for (int x = 0; x < width; x++) {
                dst[x] = static_cast<uint8_t>((levels[x] * 255 + denom / 2) / denom);
            }
            break;
        case OutFormat::I4:
            std::memset(dst, 0, out_stride(fmt, width));
            for (int x = 0; x < width; x++) {
                uint8_t v = levels[x] & 0x0F;
                if ((x & 1) == 0) dst[x >> 1] |= static_cast<uint8_t>(v << 4);
                else              dst[x >> 1] |= v;
            }
            break;
        case OutFormat::I1:
            std::memset(dst, 0, out_stride(fmt, width));
            for (int x = 0; x < width; x++) {
                if (levels[x] & 1) dst[x >> 3] |= static_cast<uint8_t>(0x80 >> (x & 7));
            }
            break;
    }
}

}  // namespace imgproc
