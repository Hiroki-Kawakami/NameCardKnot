/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "jpeg.hpp"

#include <cmath>
#include <cstring>

#include "alloc.hpp"

namespace imgproc {

static const uint8_t kZigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// IDCT basis: COS[u][x] = a(u) * cos((2x+1)u*pi/16), a(0)=0.5/sqrt2, else 0.5.
// Folded so a 2-pass separable transform yields the JPEG 1/4 scale factor.
static float g_cos[8][8];
static bool g_cos_ready = false;

static void init_cos() {
    if (g_cos_ready) return;
    for (int u = 0; u < 8; u++) {
        float a = u == 0 ? 0.353553390593f : 0.5f;
        for (int x = 0; x < 8; x++)
            g_cos[u][x] = a * std::cos((2 * x + 1) * u * 3.14159265358979f / 16.0f);
    }
    g_cos_ready = true;
}

static uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }

JpegDecoder::~JpegDecoder() {
    img_free(band_);
}

int JpegDecoder::raw() {
    uint8_t b;
    return in_->read(&b, 1) == 1 ? b : -1;
}

int JpegDecoder::u16() {
    int hi = raw(), lo = raw();
    if (hi < 0 || lo < 0) return -1;
    return (hi << 8) | lo;
}

bool JpegDecoder::skip(int n) {
    while (n-- > 0)
        if (raw() < 0) return false;
    return true;
}

bool JpegDecoder::parse_dqt(int len) {
    while (len > 0) {
        int pq_tq = raw();
        if (pq_tq < 0) return false;
        int pq = pq_tq >> 4, tq = pq_tq & 0x0F;
        if (tq > 3) return false;
        len -= 1;
        for (int k = 0; k < 64; k++) {
            int v = pq ? u16() : raw();
            if (v < 0) return false;
            qt_[tq][k] = static_cast<uint16_t>(v);
        }
        len -= pq ? 128 : 64;
    }
    return true;
}

bool JpegDecoder::parse_dht(int len) {
    while (len > 0) {
        int tc_th = raw();
        if (tc_th < 0) return false;
        int tc = tc_th >> 4, th = tc_th & 0x0F;
        if (tc > 1 || th > 3) return false;
        Huff &t = tc ? ac_[th] : dc_[th];
        int total = 0;
        t.bits[0] = 0;
        for (int i = 1; i <= 16; i++) {
            int c = raw();
            if (c < 0) return false;
            t.bits[i] = static_cast<uint8_t>(c);
            total += c;
        }
        if (total > 256) return false;
        for (int i = 0; i < total; i++) {
            int v = raw();
            if (v < 0) return false;
            t.vals[i] = static_cast<uint8_t>(v);
        }
        // Build canonical mincode/maxcode/valptr.
        int code = 0, k = 0;
        for (int l = 1; l <= 16; l++) {
            if (t.bits[l]) {
                t.valptr[l] = k;
                t.mincode[l] = code;
                code += t.bits[l];
                t.maxcode[l] = code - 1;
                k += t.bits[l];
            } else {
                t.maxcode[l] = -1;
            }
            code <<= 1;
        }
        t.defined = true;
        len -= 17 + total;
    }
    return true;
}

Status JpegDecoder::parse_sof(int len) {
    (void)len;
    int prec = raw();
    if (prec != 8) return Status::UnsupportedFormat;
    int h = u16(), w = u16();
    int nc = raw();
    if (h <= 0 || w <= 0) return Status::DecodeError;
    if (nc != 1 && nc != 3) return Status::UnsupportedFormat;
    h_ = static_cast<uint16_t>(h);
    w_ = static_cast<uint16_t>(w);
    ncomp_ = nc;
    hmax_ = vmax_ = 1;
    for (int i = 0; i < nc; i++) {
        int id = raw(), hv = raw(), tq = raw();
        if (id < 0 || hv < 0 || tq < 0) return Status::DecodeError;
        comp_[i].id = id;
        comp_[i].h = hv >> 4;
        comp_[i].v = hv & 0x0F;
        comp_[i].tq = tq;
        if (comp_[i].h < 1 || comp_[i].h > 2 || comp_[i].v < 1 || comp_[i].v > 2)
            return Status::UnsupportedFormat;
        if (comp_[i].h > hmax_) hmax_ = comp_[i].h;
        if (comp_[i].v > vmax_) vmax_ = comp_[i].v;
    }
    return Status::Ok;
}

Status JpegDecoder::parse_sos(int len) {
    (void)len;
    int ns = raw();
    if (ns != ncomp_) return Status::UnsupportedFormat;  // single full scan only
    for (int i = 0; i < ns; i++) {
        int cs = raw(), td_ta = raw();
        if (cs < 0 || td_ta < 0) return Status::DecodeError;
        int ci = -1;
        for (int j = 0; j < ncomp_; j++)
            if (comp_[j].id == cs) ci = j;
        if (ci < 0) return Status::DecodeError;
        comp_[ci].td = td_ta >> 4;
        comp_[ci].ta = td_ta & 0x0F;
    }
    raw();  // Ss
    raw();  // Se
    raw();  // Ah/Al
    return Status::Ok;
}

Status JpegDecoder::open(InputStream &in, const Options &opts) {
    init_cos();
    in_ = &in;
    alloc_caps_ = opts.alloc_caps;

    if (raw() != 0xFF || raw() != 0xD8) return Status::DecodeError;  // SOI

    bool have_sof = false;
    for (;;) {
        int b = raw();
        if (b < 0) return Status::Truncated;
        if (b != 0xFF) continue;
        int marker = raw();
        while (marker == 0xFF) marker = raw();
        if (marker < 0) return Status::Truncated;
        if (marker == 0xD9) return Status::DecodeError;        // EOI before SOS
        if (marker >= 0xD0 && marker <= 0xD7) continue;        // stray RST
        if (marker == 0x01) continue;                          // TEM, no payload

        int len = u16();
        if (len < 2) return Status::DecodeError;
        len -= 2;

        if (marker == 0xDB) {
            if (!parse_dqt(len)) return Status::DecodeError;
        } else if (marker == 0xC4) {
            if (!parse_dht(len)) return Status::DecodeError;
        } else if (marker == 0xC0) {
            Status s = parse_sof(len);
            if (s != Status::Ok) return s;
            have_sof = true;
        } else if (marker == 0xDD) {
            restart_interval_ = u16();
        } else if (marker == 0xDA) {
            if (!have_sof) return Status::DecodeError;
            Status s = parse_sos(len);
            if (s != Status::Ok) return s;
            break;  // entropy-coded data follows
        } else if ((marker >= 0xC1 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8) {
            return Status::UnsupportedFormat;  // non-baseline SOF (progressive, etc.)
        } else {
            if (!skip(len)) return Status::Truncated;  // APPn, COM, ...
        }
    }
    return setup(opts);
}

Status JpegDecoder::setup(const Options &opts) {
    // Pick the coarse decode scale: downscale toward the target, then enforce
    // the pixel cap. The pipeline does the remaining fine box-reduction.
    scale_ = 1;
    if (opts.target_w > 0 && opts.target_h > 0) {
        while (scale_ < 8 &&
               (w_ / (scale_ * 2)) >= opts.target_w &&
               (h_ / (scale_ * 2)) >= opts.target_h)
            scale_ *= 2;
    }
    if (opts.max_src_pixels) {
        while (scale_ < 8) {
            uint32_t dw = (w_ + scale_ - 1) / scale_;
            uint32_t dh = (h_ + scale_ - 1) / scale_;
            if (static_cast<uint64_t>(dw) * dh <= opts.max_src_pixels) break;
            scale_ *= 2;
        }
    }
    blk_ = 8 / scale_;

    mcus_per_row_ = (w_ + 8 * hmax_ - 1) / (8 * hmax_);
    mcu_rows_ = (h_ + 8 * vmax_ - 1) / (8 * vmax_);

    width = static_cast<uint16_t>((w_ + scale_ - 1) / scale_);
    height = static_cast<uint16_t>((h_ + scale_ - 1) / scale_);
    out_ch_ = ncomp_ == 1 ? 1 : 3;
    kind = ncomp_ == 1 ? PixelKind::Gray8 : PixelKind::RGB888;

    band_w_ = mcus_per_row_ * blk_ * hmax_;
    band_h_ = blk_ * vmax_;
    band_ = static_cast<uint8_t *>(img_alloc(static_cast<size_t>(band_w_) * band_h_ * out_ch_, alloc_caps_));
    if (!band_) return Status::OutOfMemory;

    for (int i = 0; i < ncomp_; i++) comp_[i].dcpred = 0;
    return Status::Ok;
}

int JpegDecoder::read_data_byte() {
    if (marker_pending_) return 0;
    int b = raw();
    if (b < 0) { marker_pending_ = true; marker_ = 0xD9; return 0; }
    if (b != 0xFF) return b;
    int m = raw();
    while (m == 0xFF) m = raw();
    if (m == 0x00) return 0xFF;        // stuffed FF
    if (m < 0) { marker_pending_ = true; marker_ = 0xD9; return 0; }
    marker_pending_ = true;
    marker_ = m;
    return 0;
}

int JpegDecoder::getbit() {
    if (bitcnt_ == 0) {
        bitbuf_ = read_data_byte();
        bitcnt_ = 8;
    }
    bitcnt_--;
    return (bitbuf_ >> bitcnt_) & 1;
}

int JpegDecoder::getbits(int n) {
    int v = 0;
    while (n-- > 0) v = (v << 1) | getbit();
    return v;
}

int JpegDecoder::receive_extend(int s) {
    int v = getbits(s);
    if (v < (1 << (s - 1))) v += (-1 << s) + 1;
    return v;
}

int JpegDecoder::huffdecode(const Huff &h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | getbit();
        if (h.maxcode[l] >= 0 && code <= h.maxcode[l])
            return h.vals[h.valptr[l] + code - h.mincode[l]];
    }
    err_ = true;
    return 0;
}

void JpegDecoder::idct_reduce(const int *coeff, uint8_t *dst, int dst_stride) {
    if (blk_ == 1) {  // DC only: block average = F00 / 8 (+ level shift)
        dst[0] = clamp8(static_cast<int>(std::lround(coeff[0] / 8.0f)) + 128);
        return;
    }
    float tmp[64];
    for (int y = 0; y < 8; y++) {       // 1-D IDCT on rows
        const int *row = coeff + y * 8;
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += row[u] * g_cos[u][x];
            tmp[y * 8 + x] = s;
        }
    }
    float spatial[64];
    for (int x = 0; x < 8; x++) {        // 1-D IDCT on columns
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += tmp[v * 8 + x] * g_cos[v][y];
            spatial[y * 8 + x] = s;
        }
    }
    int f = 8 / blk_;                    // box-reduce 8x8 -> blk_ x blk_
    float norm = 1.0f / (f * f);
    for (int oy = 0; oy < blk_; oy++) {
        for (int ox = 0; ox < blk_; ox++) {
            float sum = 0;
            for (int yy = 0; yy < f; yy++)
                for (int xx = 0; xx < f; xx++)
                    sum += spatial[(oy * f + yy) * 8 + ox * f + xx];
            dst[oy * dst_stride + ox] =
                clamp8(static_cast<int>(std::lround(sum * norm)) + 128);
        }
    }
}

void JpegDecoder::decode_block(int ci, uint8_t *dst, int dst_stride) {
    Comp &c = comp_[ci];
    int coeff[64];
    std::memset(coeff, 0, sizeof coeff);

    int t = huffdecode(dc_[c.td]);
    int diff = t ? receive_extend(t) : 0;
    c.dcpred += diff;
    coeff[0] = c.dcpred * qt_[c.tq][0];

    int k = 1;
    while (k < 64) {
        int rs = huffdecode(ac_[c.ta]);
        if (err_) return;
        int r = rs >> 4, s = rs & 0x0F;
        if (s == 0) {
            if (r == 15) { k += 16; continue; }  // ZRL
            break;                                // EOB
        }
        k += r;
        if (k >= 64) break;
        coeff[kZigzag[k]] = receive_extend(s) * qt_[c.tq][k];
        k++;
    }
    idct_reduce(coeff, dst, dst_stride);
}

bool JpegDecoder::consume_restart() {
    bitcnt_ = 0;  // discard partial bits
    if (marker_pending_) {
        bool ok = marker_ >= 0xD0 && marker_ <= 0xD7;
        marker_pending_ = false;
        return ok;
    }
    for (;;) {
        int b = raw();
        if (b < 0) return false;
        if (b == 0xFF) {
            int m = raw();
            while (m == 0xFF) m = raw();
            return m >= 0xD0 && m <= 0xD7;
        }
    }
}

void JpegDecoder::decode_mcu_row() {
    int base = mcu_row_idx_ * blk_ * vmax_;
    band_valid_rows_ = blk_ * vmax_;
    if (base + band_valid_rows_ > static_cast<int>(height))
        band_valid_rows_ = static_cast<int>(height) - base;

    for (int mx = 0; mx < mcus_per_row_ && !err_; mx++) {
        if (restart_interval_ && mcu_count_ > 0 && mcu_count_ % restart_interval_ == 0) {
            if (!consume_restart()) { err_ = true; return; }
            for (int i = 0; i < ncomp_; i++) comp_[i].dcpred = 0;
        }
        for (int ci = 0; ci < ncomp_; ci++) {
            Comp &c = comp_[ci];
            int cb_w = c.h * blk_;
            for (int by = 0; by < c.v; by++)
                for (int bx = 0; bx < c.h; bx++)
                    decode_block(ci, compbuf_[ci] + (by * blk_) * cb_w + bx * blk_, cb_w);
        }

        // Assemble MCU pixels: nearest-neighbour chroma upsample + YCbCr->RGB.
        int px0 = mx * blk_ * hmax_;
        for (int py = 0; py < band_h_; py++) {
            uint8_t *out = band_ + (static_cast<size_t>(py) * band_w_ + px0) * out_ch_;
            for (int px = 0; px < blk_ * hmax_; px++) {
                int y0 = compbuf_[0][(py * comp_[0].v / vmax_) * (comp_[0].h * blk_) +
                                     (px * comp_[0].h / hmax_)];
                if (out_ch_ == 1) {
                    *out++ = static_cast<uint8_t>(y0);
                } else {
                    int cb = compbuf_[1][(py * comp_[1].v / vmax_) * (comp_[1].h * blk_) +
                                         (px * comp_[1].h / hmax_)] - 128;
                    int cr = compbuf_[2][(py * comp_[2].v / vmax_) * (comp_[2].h * blk_) +
                                         (px * comp_[2].h / hmax_)] - 128;
                    *out++ = clamp8(y0 + ((91881 * cr) >> 16));
                    *out++ = clamp8(y0 - ((22554 * cb + 46802 * cr) >> 16));
                    *out++ = clamp8(y0 + ((116130 * cb) >> 16));
                }
            }
        }
        mcu_count_++;
    }
    band_row_ = 0;
    mcu_row_idx_++;
}

bool JpegDecoder::next_row(uint8_t *dst) {
    if (err_ || out_row_ >= height) return false;
    if (band_row_ >= band_valid_rows_) {
        if (mcu_row_idx_ >= mcu_rows_) return false;
        decode_mcu_row();
        if (err_) return false;
    }
    std::memcpy(dst, band_ + static_cast<size_t>(band_row_) * band_w_ * out_ch_,
                static_cast<size_t>(width) * out_ch_);
    band_row_++;
    out_row_++;
    return true;
}

}  // namespace imgproc
