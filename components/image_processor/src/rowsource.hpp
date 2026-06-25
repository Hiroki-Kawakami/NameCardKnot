/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdint>
#include <memory>

#include "image_processor.hpp"
#include "sniff.hpp"

namespace imgproc {

class InputStream;

enum class PixelKind { Gray8, RGB888 };

// Top-to-bottom row supplier feeding the downscale -> color -> dither pipeline.
// next_row() fills width*channels() bytes; decoders and the Phase 2 synthetic
// source both implement this.
struct RowSource {
    uint16_t  width = 0;
    uint16_t  height = 0;
    PixelKind kind = PixelKind::RGB888;

    virtual ~RowSource() = default;
    virtual bool next_row(uint8_t *dst) = 0;  // false = end of image / error

    int channels() const { return kind == PixelKind::Gray8 ? 1 : 3; }
};

// A decoder is a RowSource with a header-parse step. open() reads the container
// header from `in`, fills width/height/kind, and binds the stream for the
// subsequent next_row() calls.
struct Decoder : RowSource {
    virtual Status open(InputStream &in) = 0;
};

// Phase 1 returns nullptr (no decoder wired yet); PNG/JPEG arrive in Phase 3/4.
std::unique_ptr<Decoder> make_decoder(Format fmt);

}  // namespace imgproc
