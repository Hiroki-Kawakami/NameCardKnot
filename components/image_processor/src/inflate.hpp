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
    // Returns a pointer to the next run of >= 1 input bytes and sets *n; *n == 0
    // (pointer may be null) at end of input. Block-based so the decoder reads via
    // a pointer instead of a virtual call per byte.
    virtual const uint8_t *refill(size_t *n) = 0;
};

class Inflate {
public:
    bool init(ByteSource *src);          // false: bad/unsupported zlib header
    size_t read(uint8_t *out, size_t n); // bytes produced (< n only at stream end)
    bool failed() const { return err_; }
    bool ended() const { return ended_; }

private:
    static constexpr int kFastBits = 9;
    static constexpr int kFastSize = 1 << kFastBits;

    struct Huff {
        int16_t  count[16];
        int16_t  symbol[288];
        uint16_t fast[kFastSize];  // (len << 9) | symbol, indexed by kFastBits LSB-first bits; 0 = miss
    };

    int      next_byte();  // one input byte from the block buffer, or -1 at end
    uint32_t bits(int need);
    int      decode(const Huff &h);
    int      decode_slow(const Huff &h);
    void     build_fixed();
    bool     build_dynamic();
    static int construct(Huff &h, const uint8_t *lengths, int n);

    ByteSource *src_ = nullptr;
    const uint8_t *in_ptr_ = nullptr;  // current position in the source block
    size_t   in_avail_ = 0;            // bytes left in the current block
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
