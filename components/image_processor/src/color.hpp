/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdint>

#include "image_processor.hpp"

namespace imgproc {

// Per-pixel color conversion: linearize input channels, weight to luminance,
// then encode to perceptual gray. To keep box averaging correct, the work is
// split around the downsampler:
//   - to_intensity(): source row -> per-pixel intensity the downsampler averages.
//       linear_downsample=true  -> linear luminance (0..65535), gamma deferred.
//       linear_downsample=false -> perceptual gray (0..255), gamma applied here.
//   - finalize(): averaged intensity -> final gray8 (+ output gamma when deferred,
//       + invert).
class ColorPipeline {
public:
    void init(const Options &opts);

    // src: source row (channels==1 Gray8, or ==3 RGB888); writes width intensities.
    void to_intensity(const uint8_t *src, int width, int channels, uint16_t *dst) const;

    // avg: downsampled intensity in the domain to_intensity produced.
    uint8_t finalize(uint32_t avg) const;

private:
    uint16_t in_lut_[256];   // channel value -> linear (0..65535)
    uint8_t  out_lut_[256];  // linear>>8 -> perceptual gray (0..255)
    uint32_t wr_, wg_, wb_;  // luma weights, Q15 (sum == 32768)
    bool     linear_downsample_ = true;
    bool     invert_ = false;
};

}  // namespace imgproc
