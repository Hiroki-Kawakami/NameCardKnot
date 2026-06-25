/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "png.hpp"

#include <cstring>
#include <new>

namespace imgproc {

static uint32_t be32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

static bool read_full(InputStream &in, uint8_t *buf, size_t n) {
    return in.read(buf, n) == n;
}

static bool skip(InputStream &in, size_t n) {
    uint8_t tmp[64];
    while (n) {
        size_t k = n < sizeof tmp ? n : sizeof tmp;
        if (in.read(tmp, k) != k) return false;
        n -= k;
    }
    return true;
}

static bool type_is(const uint8_t *t, char a, char b, char c, char d) {
    return t[0] == a && t[1] == b && t[2] == c && t[3] == d;
}

static uint8_t composite_white(uint8_t c, uint8_t a) {
    return static_cast<uint8_t>((c * a + 255 * (255 - a) + 127) / 255);
}

int PngDecoder::IdatSource::get() {
    while (remaining == 0) {
        if (done) return -1;
        uint8_t hdr[8];  // CRC of the finished chunk (4) + next length (4)
        if (in->read(hdr, 8) != 8) { done = true; return -1; }
        uint32_t len = be32(hdr + 4);
        uint8_t type[4];
        if (in->read(type, 4) != 4) { done = true; return -1; }
        if (type_is(type, 'I', 'D', 'A', 'T')) {
            remaining = len;
        } else {
            done = true;  // first non-IDAT chunk ends the image data
            return -1;
        }
    }
    uint8_t b;
    if (in->read(&b, 1) != 1) { done = true; return -1; }
    remaining--;
    return b;
}

Status PngDecoder::open(InputStream &in, const Options &opts) {
    (void)opts;  // PNG decodes at full resolution; the pipeline does the downscale.
    static const uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    uint8_t sig[8];
    if (!read_full(in, sig, 8)) return Status::Truncated;
    if (std::memcmp(sig, kSig, 8) != 0) return Status::DecodeError;

    // IHDR must be the first chunk.
    uint8_t lenbuf[4], type[4];
    if (!read_full(in, lenbuf, 4) || !read_full(in, type, 4)) return Status::Truncated;
    if (!type_is(type, 'I', 'H', 'D', 'R') || be32(lenbuf) != 13) return Status::DecodeError;
    uint8_t ih[13];
    if (!read_full(in, ih, 13)) return Status::Truncated;
    if (!skip(in, 4)) return Status::Truncated;  // CRC

    uint32_t w = be32(ih), h = be32(ih + 4);
    bitdepth_ = ih[8];
    colortype_ = ih[9];
    uint8_t comp = ih[10], filt = ih[11], interlace = ih[12];
    if (comp != 0 || filt != 0) return Status::DecodeError;
    if (interlace != 0) return Status::UnsupportedFormat;
    if (w == 0 || h == 0) return Status::DecodeError;
    if (w > 0xFFFF || h > 0xFFFF) return Status::TooLarge;

    // Validate colortype / bitdepth and set channel layout.
    switch (colortype_) {
        case 0:  // grayscale
            if (bitdepth_ != 1 && bitdepth_ != 2 && bitdepth_ != 4 && bitdepth_ != 8)
                return Status::UnsupportedFormat;
            src_channels_ = 1; out_rgb_ = false; break;
        case 2:  // truecolor
            if (bitdepth_ != 8) return Status::UnsupportedFormat;
            src_channels_ = 3; out_rgb_ = true; break;
        case 3:  // palette
            if (bitdepth_ != 1 && bitdepth_ != 2 && bitdepth_ != 4 && bitdepth_ != 8)
                return Status::UnsupportedFormat;
            src_channels_ = 1; out_rgb_ = true; break;
        case 4:  // grayscale + alpha
            if (bitdepth_ != 8) return Status::UnsupportedFormat;
            src_channels_ = 2; out_rgb_ = false; break;
        case 6:  // truecolor + alpha
            if (bitdepth_ != 8) return Status::UnsupportedFormat;
            src_channels_ = 4; out_rgb_ = true; break;
        default:
            return Status::UnsupportedFormat;
    }

    std::memset(trns_, 0xFF, sizeof trns_);

    // Walk ancillary chunks until the first IDAT.
    for (;;) {
        if (!read_full(in, lenbuf, 4) || !read_full(in, type, 4)) return Status::Truncated;
        uint32_t len = be32(lenbuf);
        if (type_is(type, 'P', 'L', 'T', 'E')) {
            if (len % 3 != 0 || len / 3 > 256) return Status::DecodeError;
            palette_count_ = static_cast<int>(len / 3);
            for (int i = 0; i < palette_count_; i++)
                if (!read_full(in, palette_[i], 3)) return Status::Truncated;
            if (!skip(in, 4)) return Status::Truncated;
        } else if (type_is(type, 't', 'R', 'N', 'S')) {
            uint32_t k = len > 256 ? 256 : len;
            if (k && in.read(trns_, k) != k) return Status::Truncated;
            if (!skip(in, len - k + 4)) return Status::Truncated;
        } else if (type_is(type, 'I', 'D', 'A', 'T')) {
            idat_.in = &in;
            idat_.remaining = len;
            break;
        } else if (type_is(type, 'I', 'E', 'N', 'D')) {
            return Status::DecodeError;  // no image data
        } else {
            if (!skip(in, len + 4)) return Status::Truncated;
        }
    }

    if (colortype_ == 3 && palette_count_ == 0) return Status::DecodeError;
    if (!inflate_.init(&idat_)) return Status::DecodeError;

    width = static_cast<uint16_t>(w);
    height = static_cast<uint16_t>(h);
    kind = out_rgb_ ? PixelKind::RGB888 : PixelKind::Gray8;

    rowbytes_ = (static_cast<size_t>(w) * src_channels_ * bitdepth_ + 7) / 8;
    filt_bpp_ = (src_channels_ * bitdepth_ + 7) / 8;
    row_ = 0;

    prev_.reset(new (std::nothrow) uint8_t[rowbytes_]());
    cur_.reset(new (std::nothrow) uint8_t[rowbytes_]);
    filtbuf_.reset(new (std::nothrow) uint8_t[1 + rowbytes_]);
    if (!prev_ || !cur_ || !filtbuf_) return Status::OutOfMemory;
    return Status::Ok;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = static_cast<int>(a) + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

void PngDecoder::unfilter(uint8_t filter, const uint8_t *src, uint8_t *dst) {
    int bpp = filt_bpp_;
    for (size_t i = 0; i < rowbytes_; i++) {
        uint8_t a = i >= static_cast<size_t>(bpp) ? dst[i - bpp] : 0;
        uint8_t b = prev_[i];
        uint8_t c = i >= static_cast<size_t>(bpp) ? prev_[i - bpp] : 0;
        uint8_t x = src[i];
        switch (filter) {
            case 0: dst[i] = x; break;
            case 1: dst[i] = static_cast<uint8_t>(x + a); break;
            case 2: dst[i] = static_cast<uint8_t>(x + b); break;
            case 3: dst[i] = static_cast<uint8_t>(x + ((a + b) >> 1)); break;
            case 4: dst[i] = static_cast<uint8_t>(x + paeth(a, b, c)); break;
            default: dst[i] = x; break;
        }
    }
}

void PngDecoder::convert_row(const uint8_t *line, uint8_t *dst) {
    int w = width;
    switch (colortype_) {
        case 0: {  // grayscale, 1/2/4/8-bit
            int maxv = (1 << bitdepth_) - 1;
            for (int x = 0; x < w; x++) {
                int bitpos = x * bitdepth_;
                int v = (line[bitpos >> 3] >> (8 - bitdepth_ - (bitpos & 7))) & maxv;
                dst[x] = static_cast<uint8_t>(v * 255 / maxv);
            }
            break;
        }
        case 2:  // RGB8
            std::memcpy(dst, line, static_cast<size_t>(w) * 3);
            break;
        case 3: {  // palette, 1/2/4/8-bit index
            int maxv = (1 << bitdepth_) - 1;
            for (int x = 0; x < w; x++) {
                int bitpos = x * bitdepth_;
                int idx = (line[bitpos >> 3] >> (8 - bitdepth_ - (bitpos & 7))) & maxv;
                if (idx >= palette_count_) idx = palette_count_ - 1;
                uint8_t a = trns_[idx];
                dst[3 * x + 0] = composite_white(palette_[idx][0], a);
                dst[3 * x + 1] = composite_white(palette_[idx][1], a);
                dst[3 * x + 2] = composite_white(palette_[idx][2], a);
            }
            break;
        }
        case 4:  // gray + alpha
            for (int x = 0; x < w; x++)
                dst[x] = composite_white(line[2 * x], line[2 * x + 1]);
            break;
        case 6:  // RGB + alpha
            for (int x = 0; x < w; x++) {
                uint8_t a = line[4 * x + 3];
                dst[3 * x + 0] = composite_white(line[4 * x + 0], a);
                dst[3 * x + 1] = composite_white(line[4 * x + 1], a);
                dst[3 * x + 2] = composite_white(line[4 * x + 2], a);
            }
            break;
        default:
            break;
    }
}

bool PngDecoder::next_row(uint8_t *dst) {
    if (row_ >= height) return false;
    size_t need = 1 + rowbytes_;
    if (inflate_.read(filtbuf_.get(), need) != need || inflate_.failed()) return false;

    uint8_t filter = filtbuf_[0];
    unfilter(filter, filtbuf_.get() + 1, cur_.get());
    convert_row(cur_.get(), dst);
    prev_.swap(cur_);
    row_++;
    return true;
}

}  // namespace imgproc
