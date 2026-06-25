/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdint>
#include <memory>

#include "image_processor.hpp"

namespace imgproc {

// Quantizes a stream of gray8 rows (top-to-bottom) to N levels with a selectable
// method. Integer-only (no float): quantize is a 256-entry LUT, error diffusion
// carries fixed-point (<<8) error in int32 buffers. Ordered (Bayer) is stateless;
// error-diffusion carries error into the next up-to-2 rows via a 3-row ring, so
// rows must be fed in order.
class Ditherer {
public:
    bool init(const Options &opts, int width);

    // gray: width gray8 values; y: output row index; out: width levels (0..N-1).
    void process_row(const uint8_t *gray, int y, uint8_t *out);

    int levels() const { return n_; }

private:
    int     n_ = 16;
    int     width_ = 0;
    Dither  method_ = Dither::FloydSteinberg;
    bool    serpentine_ = true;

    uint8_t qlut_[256];  // clamped gray -> level (0..n-1)
    uint8_t rlut_[256];  // level -> reconstructed gray (matches the L8 packer)

    const uint8_t *bmat_ = nullptr;  // ordered matrix
    int  bm_ = 0;                    // matrix size
    int  bthr_[64];                  // per-cell integer thresholds (×255)

    std::unique_ptr<int32_t[]> err_;  // 3 * width_, fixed-point error (value << 8)
    int ring0_ = 0;
    int div_ = 16;        // error-diffusion divisor
    int div_shift_ = 4;   // log2(div_) if a power of two, else -1

    void diffuse_row(const uint8_t *gray, int y, uint8_t *out);
    void ordered_row(const uint8_t *gray, int y, uint8_t *out);
};

}  // namespace imgproc
