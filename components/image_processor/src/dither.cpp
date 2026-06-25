/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dither.hpp"

#include <cstring>

namespace imgproc {

// Error-diffusion kernel: forward neighbours only (dy >= 0; dy==0 is to the
// right). Weights are normalized by `div`. dx is mirrored on right-to-left rows.
struct DiffCell { int dx, dy, w; };
struct DiffKernel { const DiffCell *cells; int count; int div; };

static const DiffCell kFS[]      = {{1,0,7},{-1,1,3},{0,1,5},{1,1,1}};
static const DiffCell kAtkinson[]= {{1,0,1},{2,0,1},{-1,1,1},{0,1,1},{1,1,1},{0,2,1}};
static const DiffCell kSierra[]  = {{1,0,5},{2,0,3},
                                    {-2,1,2},{-1,1,4},{0,1,5},{1,1,4},{2,1,2},
                                    {-1,2,2},{0,2,3},{1,2,2}};
static const DiffCell kJJN[]     = {{1,0,7},{2,0,5},
                                    {-2,1,3},{-1,1,5},{0,1,7},{1,1,5},{2,1,3},
                                    {-2,2,1},{-1,2,3},{0,2,5},{1,2,3},{2,2,1}};
static const DiffCell kStucki[]  = {{1,0,8},{2,0,4},
                                    {-2,1,2},{-1,1,4},{0,1,8},{1,1,4},{2,1,2},
                                    {-2,2,1},{-1,2,2},{0,2,4},{1,2,2},{2,2,1}};

static DiffKernel kernel_for(Dither m) {
    switch (m) {
        case Dither::Atkinson: return {kAtkinson, 6, 8};
        case Dither::Sierra:   return {kSierra, 10, 32};
        case Dither::JJN:      return {kJJN, 12, 48};
        case Dither::Stucki:   return {kStucki, 13, 42};
        case Dither::FloydSteinberg: default: return {kFS, 4, 16};
    }
}

// Recursively-generated ordered (Bayer) matrices, values 0..M*M-1.
static const uint8_t kBayer2[4]  = {0,2,3,1};
static const uint8_t kBayer4[16] = { 0, 8, 2,10, 12, 4,14, 6,
                                      3,11, 1, 9, 15, 7,13, 5};
static const uint8_t kBayer8[64] = {
     0,48,12,60, 3,51,15,63, 32,16,44,28,35,19,47,31,
     8,56, 4,52,11,59, 7,55, 40,24,36,20,43,27,39,23,
     2,50,14,62, 1,49,13,61, 34,18,46,30,33,17,45,29,
    10,58, 6,54, 9,57, 5,53, 42,26,38,22,41,25,37,21};

static void bayer_for(Dither m, const uint8_t **mat, int *size) {
    switch (m) {
        case Dither::Bayer2: *mat = kBayer2; *size = 2; break;
        case Dither::Bayer8: *mat = kBayer8; *size = 8; break;
        case Dither::Bayer4: default: *mat = kBayer4; *size = 4; break;
    }
}

bool Ditherer::init(const Options &opts, int width) {
    n_ = opts.levels < 2 ? 2 : opts.levels;
    width_ = width;
    method_ = opts.dither;
    serpentine_ = opts.serpentine;
    ring0_ = 0;

    // quantize / reconstruct LUTs (rlut matches the L8 packer's level->gray).
    int d = n_ - 1;
    for (int v = 0; v < 256; v++) {
        int level = (v * d * 2 + 255) / 510;  // round(v*(n-1)/255)
        if (level > d) level = d;
        qlut_[v] = static_cast<uint8_t>(level);
    }
    for (int l = 0; l < n_; l++) rlut_[l] = static_cast<uint8_t>((l * 255 + d / 2) / d);

    switch (method_) {
        case Dither::None:
            err_.reset();
            break;
        case Dither::Bayer2:
        case Dither::Bayer4:
        case Dither::Bayer8: {
            err_.reset();
            bayer_for(method_, &bmat_, &bm_);
            int cells = bm_ * bm_;
            for (int i = 0; i < cells; i++)  // threshold in level-step units, ×255
                bthr_[i] = (255 * (2 * bmat_[i] + 1) - 255 * cells) / (2 * cells);
            break;
        }
        default:
            err_.reset(new (std::nothrow) int32_t[3 * static_cast<size_t>(width)]());
            if (!err_) return false;
            break;
    }
    return true;
}

void Ditherer::ordered_row(const uint8_t *gray, int y, uint8_t *out) {
    if (method_ == Dither::None) {
        for (int x = 0; x < width_; x++) out[x] = qlut_[gray[x]];
        return;
    }
    int d = n_ - 1;
    for (int x = 0; x < width_; x++) {
        int num = gray[x] * d + bthr_[(y % bm_) * bm_ + (x % bm_)];
        int level = num <= 0 ? 0 : (num * 2 + 255) / 510;  // round(num/255)
        if (level > d) level = d;
        out[x] = static_cast<uint8_t>(level);
    }
}

void Ditherer::diffuse_row(const uint8_t *gray, int y, uint8_t *out) {
    int32_t *cur = err_.get() + static_cast<size_t>(ring0_) * width_;
    int32_t *n1  = err_.get() + static_cast<size_t>((ring0_ + 1) % 3) * width_;
    int32_t *n2  = err_.get() + static_cast<size_t>((ring0_ + 2) % 3) * width_;

    DiffKernel k = kernel_for(method_);
    int dir = (serpentine_ && (y & 1)) ? -1 : 1;
    int xstart = dir > 0 ? 0 : width_ - 1;
    int xend = dir > 0 ? width_ : -1;

    for (int x = xstart; x != xend; x += dir) {
        int vf = (gray[x] << 8) + cur[x];  // value in 1/256 units
        if (vf < 0) vf = 0;
        else if (vf > (255 << 8)) vf = 255 << 8;
        int level = qlut_[(vf + 128) >> 8];
        out[x] = static_cast<uint8_t>(level);
        int ef = vf - (rlut_[level] << 8);  // fixed-point error
        for (int i = 0; i < k.count; i++) {
            int tx = x + dir * k.cells[i].dx;
            if (tx < 0 || tx >= width_) continue;
            int add = ef * k.cells[i].w / k.div;
            switch (k.cells[i].dy) {
                case 0: cur[tx] += add; break;
                case 1: n1[tx] += add; break;
                default: n2[tx] += add; break;
            }
        }
    }

    std::memset(cur, 0, sizeof(int32_t) * width_);  // consumed; reused as n2 next row
    ring0_ = (ring0_ + 1) % 3;
}

void Ditherer::process_row(const uint8_t *gray, int y, uint8_t *out) {
    if (err_) diffuse_row(gray, y, out);
    else      ordered_row(gray, y, out);
}

}  // namespace imgproc
