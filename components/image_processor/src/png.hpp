/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdint>
#include <memory>

#include "inflate.hpp"
#include "rowsource.hpp"
#include "stream.hpp"

namespace imgproc {

// Self-contained PNG decoder: streaming inflate -> scanline unfilter -> per-row
// color normalization, all single-pass over the InputStream. Supports 8-bit
// grayscale/RGB/RGBA, 1/2/4/8-bit grayscale and palette, with tRNS composited
// over white. Unsupported (returns from open()): 16-bit channels, interlaced.
class PngDecoder : public Decoder {
public:
    Status open(InputStream &in, const Options &opts) override;
    bool next_row(uint8_t *dst) override;

private:
    // Presents the concatenated IDAT payloads as one byte stream to inflate,
    // walking chunk boundaries and stopping at the first non-IDAT chunk.
    struct IdatSource : ByteSource {
        InputStream *in = nullptr;
        uint32_t remaining = 0;  // bytes left in the current IDAT chunk
        bool done = false;
        uint8_t buf[2048];       // block handed to the inflater
        const uint8_t *refill(size_t *n) override;
    };

    void unfilter(uint8_t filter, const uint8_t *src, uint8_t *dst);
    void convert_row(const uint8_t *line, uint8_t *dst);

    IdatSource idat_;
    Inflate inflate_;

    uint8_t bitdepth_ = 8;
    uint8_t colortype_ = 0;
    int     src_channels_ = 1;  // channels per pixel as stored (for filtering)
    bool    out_rgb_ = false;
    size_t  rowbytes_ = 0;
    int     filt_bpp_ = 1;
    uint32_t row_ = 0;

    uint8_t  palette_[256][3];
    uint8_t  trns_[256];
    int      palette_count_ = 0;

    std::unique_ptr<uint8_t[]> prev_;     // previous reconstructed scanline
    std::unique_ptr<uint8_t[]> cur_;      // current reconstructed scanline
    std::unique_ptr<uint8_t[]> filtbuf_;  // 1 filter byte + filtered scanline
};

}  // namespace imgproc
