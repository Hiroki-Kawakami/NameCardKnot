/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "color.hpp"

#include <cmath>

namespace imgproc {

static float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float c) {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void ColorPipeline::init(const Options &opts) {
    linear_downsample_ = opts.linear_downsample;
    invert_ = opts.invert;

    // Input EOTF -> linear (0..65535).
    for (int i = 0; i < 256; i++) {
        float c = i / 255.0f, lin;
        switch (opts.in_gamma) {
            case GammaIn::Srgb:   lin = srgb_to_linear(c); break;
            case GammaIn::Power:  lin = std::pow(c, opts.in_power); break;
            case GammaIn::Linear: default: lin = c; break;
        }
        int v = static_cast<int>(std::lround(clamp01(lin) * 65535.0f));
        in_lut_[i] = static_cast<uint16_t>(v);
    }

    // linear (indexed by top 8 bits) -> perceptual gray (0..255).
    for (int i = 0; i < 256; i++) {
        float lin = i / 255.0f, g;
        switch (opts.out_gamma) {
            case GammaOut::Srgb:  g = linear_to_srgb(lin); break;
            case GammaOut::Power: g = std::pow(lin, 1.0f / opts.out_power); break;
            case GammaOut::EpdLut:
                if (opts.epd_lut16) {
                    // Piecewise-linear over 16 measured points at lin = k/15.
                    float fk = lin * 15.0f;
                    int k = static_cast<int>(fk);
                    if (k >= 15) { g = opts.epd_lut16[15] / 255.0f; }
                    else {
                        float t = fk - k;
                        g = ((1.0f - t) * opts.epd_lut16[k] + t * opts.epd_lut16[k + 1]) / 255.0f;
                    }
                } else {
                    g = linear_to_srgb(lin);  // no table -> sRGB fallback
                }
                break;
            default: g = linear_to_srgb(lin); break;
        }
        int v = static_cast<int>(std::lround(clamp01(g) * 255.0f));
        out_lut_[i] = static_cast<uint8_t>(v);
    }

    // Luma weights -> Q15, forcing the sum to 32768 (wb absorbs rounding).
    float w[3];
    switch (opts.luma) {
        case Luma::Rec709:  w[0] = 0.2126f; w[1] = 0.7152f; w[2] = 0.0722f; break;
        case Luma::Rec601:  w[0] = 0.299f;  w[1] = 0.587f;  w[2] = 0.114f;  break;
        case Luma::Average: w[0] = w[1] = w[2] = 1.0f / 3.0f; break;
        case Luma::Custom: default:
            w[0] = opts.luma_custom[0]; w[1] = opts.luma_custom[1]; w[2] = opts.luma_custom[2];
            break;
    }
    float sum = w[0] + w[1] + w[2];
    if (sum <= 0.0f) { w[0] = 0.2126f; w[1] = 0.7152f; w[2] = 0.0722f; sum = 1.0f; }
    wr_ = static_cast<uint32_t>(std::lround(w[0] / sum * 32768.0f));
    wg_ = static_cast<uint32_t>(std::lround(w[1] / sum * 32768.0f));
    wb_ = 32768u - wr_ - wg_;
}

void ColorPipeline::to_intensity(const uint8_t *src, int width, int channels, uint16_t *dst) const {
    for (int x = 0; x < width; x++) {
        uint32_t lum16;
        if (channels == 1) {
            lum16 = in_lut_[src[x]];
        } else {
            const uint8_t *p = src + x * 3;
            lum16 = (wr_ * in_lut_[p[0]] + wg_ * in_lut_[p[1]] + wb_ * in_lut_[p[2]]) >> 15;
        }
        dst[x] = linear_downsample_ ? static_cast<uint16_t>(lum16)
                                    : out_lut_[lum16 >> 8];
    }
}

uint8_t ColorPipeline::finalize(uint32_t avg) const {
    uint8_t g = linear_downsample_ ? out_lut_[avg >> 8]
                                   : static_cast<uint8_t>(avg > 255 ? 255 : avg);
    return invert_ ? static_cast<uint8_t>(255 - g) : g;
}

}  // namespace imgproc
