/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

// Streaming DEFLATE (RFC 1951) decompressor — decompression only, pull-based:
// read() yields up to n bytes and resumes mid-block on the next call, so a PNG
// decoder can pull one scanline at a time without buffering the whole image.
// Wraps a zlib stream (RFC 1950): init() consumes/validates the 2-byte header.
namespace imgproc {

struct ByteSource {
    virtual ~ByteSource() = default;
    virtual int get() = 0;  // next byte (0..255), or < 0 at end of input
};

class Inflate {
public:
    bool init(ByteSource *src);          // false: bad/unsupported zlib header
    size_t read(uint8_t *out, size_t n); // bytes produced (< n only at stream end)
    bool failed() const { return err_; }
    bool ended() const { return ended_; }

private:
    struct Huff {
        int16_t count[16];
        int16_t symbol[288];
    };

    uint32_t bits(int need);
    int      decode(const Huff &h);
    void     build_fixed();
    bool     build_dynamic();
    static int construct(Huff &h, const uint8_t *lengths, int n);

    ByteSource *src_ = nullptr;
    uint32_t bitbuf_ = 0;
    int      bitcnt_ = 0;
    bool     eof_ = false;

    std::unique_ptr<uint8_t[]> win_;  // 32 KiB sliding window
    uint32_t wpos_ = 0;

    Huff lit_{};
    Huff dist_{};

    bool block_active_ = false;
    int  bfinal_ = 0;
    int  mode_ = 0;          // 0 = stored, 1 = compressed
    uint32_t stored_rem_ = 0;
    int  copy_rem_ = 0;
    int  copy_dist_ = 0;

    bool ended_ = false;
    bool err_ = false;
};

}  // namespace imgproc
