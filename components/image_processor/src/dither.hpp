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
// method. Ordered (Bayer) is stateless; error-diffusion carries error into the
// next up-to-2 rows via a 3-row ring, so rows must be fed in order.
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

    // Error-diffusion state: ring of 3 rows (current + 2 ahead).
    std::unique_ptr<float[]> err_;  // 3 * width_
    int ring0_ = 0;

    int quantize(float v, float *recon) const;
    void diffuse_row(const uint8_t *gray, int y, uint8_t *out);
    void ordered_row(const uint8_t *gray, int y, uint8_t *out);
};

}  // namespace imgproc
