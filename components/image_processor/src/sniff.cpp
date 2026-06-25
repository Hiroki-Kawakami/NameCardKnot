/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "sniff.hpp"

namespace imgproc {

Format sniff(const uint8_t *hdr, size_t n) {
    static const uint8_t kPng[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (n >= 8) {
        bool png = true;
        for (int i = 0; i < 8; i++) png &= hdr[i] == kPng[i];
        if (png) return Format::Png;
    }
    if (n >= 3 && hdr[0] == 0xFF && hdr[1] == 0xD8 && hdr[2] == 0xFF) {
        return Format::Jpeg;
    }
    return Format::Unknown;
}

Status check_src_size(uint32_t w, uint32_t h, uint32_t max_pixels) {
    if (w == 0 || h == 0) return Status::DecodeError;
    if (max_pixels && static_cast<uint64_t>(w) * h > max_pixels) return Status::TooLarge;
    return Status::Ok;
}

}  // namespace imgproc
