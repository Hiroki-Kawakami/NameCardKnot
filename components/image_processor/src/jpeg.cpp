/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "jpeg.hpp"

#include <cmath>
#include <cstring>

#include "alloc.hpp"
#include "profile.hpp"

namespace imgproc {

static const uint8_t kZigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }

// AAN per-frequency scale factors; folded into the dequant table so the fast
// IDCT (jidctfst-style) needs no per-output scaling.
static const double kAanScale[8] = {
    1.0, 1.387039845, 1.306562965, 1.175875602,
    1.0, 0.785694958, 0.541196100, 0.275899379};

// Fixed-point multiply constants (×256) for the AAN butterfly.
static const int kFix1_082 = 277;
static const int kFix1_414 = 362;
static const int kFix1_847 = 473;
static const int kFix2_613 = 669;

static inline int imul(int v, int c) { return static_cast<int>((static_cast<int64_t>(v) * c) >> 8); }
static inline int idescale(int x, int n) { return (x + (1 << (n - 1))) >> n; }

// jidctfst (Arai-Agui-Nakajima) inverse DCT. `coeff` are raw (un-dequantized)
// coefficients in natural order; `q` is the AAN-prescaled dequant table. Writes
// the level-shift-free spatial block to `out` (caller adds 128 / clamps).
static void idct8x8_fast(const int *coeff, const int *q, int *out) {
    int ws[64];
    for (int c = 0; c < 8; c++) {  // pass 1: columns
        const int *in = coeff + c;
        const int *qq = q + c;
        if (!in[8] && !in[16] && !in[24] && !in[32] && !in[40] && !in[48] && !in[56]) {
            int dc = in[0] * qq[0];  // AC-free column: constant
            for (int r = 0; r < 8; r++) ws[c + r * 8] = dc;
            continue;
        }
        int tmp0 = in[0] * qq[0];
        int tmp1 = in[16] * qq[16];
        int tmp2 = in[32] * qq[32];
        int tmp3 = in[48] * qq[48];
        int tmp10 = tmp0 + tmp2, tmp11 = tmp0 - tmp2;
        int tmp13 = tmp1 + tmp3;
        int tmp12 = imul(tmp1 - tmp3, kFix1_414) - tmp13;
        tmp0 = tmp10 + tmp13;
        tmp3 = tmp10 - tmp13;
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;
        int tmp4 = in[8] * qq[8];
        int tmp5 = in[24] * qq[24];
        int tmp6 = in[40] * qq[40];
        int tmp7 = in[56] * qq[56];
        int z13 = tmp6 + tmp5, z10 = tmp6 - tmp5;
        int z11 = tmp4 + tmp7, z12 = tmp4 - tmp7;
        tmp7 = z11 + z13;
        tmp11 = imul(z11 - z13, kFix1_414);
        int z5 = imul(z10 + z12, kFix1_847);
        tmp10 = imul(z12, kFix1_082) - z5;
        tmp12 = imul(z10, -kFix2_613) + z5;
        tmp6 = tmp12 - tmp7;
        tmp5 = tmp11 - tmp6;
        tmp4 = tmp10 + tmp5;
        ws[c + 0 * 8] = tmp0 + tmp7;
        ws[c + 7 * 8] = tmp0 - tmp7;
        ws[c + 1 * 8] = tmp1 + tmp6;
        ws[c + 6 * 8] = tmp1 - tmp6;
        ws[c + 2 * 8] = tmp2 + tmp5;
        ws[c + 5 * 8] = tmp2 - tmp5;
        ws[c + 4 * 8] = tmp3 + tmp4;
        ws[c + 3 * 8] = tmp3 - tmp4;
    }
    for (int r = 0; r < 8; r++) {  // pass 2: rows
        const int *w = ws + r * 8;
        int *o = out + r * 8;
        int tmp10 = w[0] + w[4], tmp11 = w[0] - w[4];
        int tmp13 = w[2] + w[6];
        int tmp12 = imul(w[2] - w[6], kFix1_414) - tmp13;
        int tmp0 = tmp10 + tmp13, tmp3 = tmp10 - tmp13;
        int tmp1 = tmp11 + tmp12, tmp2 = tmp11 - tmp12;
        int z13 = w[5] + w[3], z10 = w[5] - w[3];
        int z11 = w[1] + w[7], z12 = w[1] - w[7];
        int tmp7 = z11 + z13;
        int tmp11b = imul(z11 - z13, kFix1_414);
        int z5 = imul(z10 + z12, kFix1_847);
        int tmp10b = imul(z12, kFix1_082) - z5;
        int tmp12b = imul(z10, -kFix2_613) + z5;
        int tmp6 = tmp12b - tmp7;
        int tmp5 = tmp11b - tmp6;
        int tmp4 = tmp10b + tmp5;
        o[0] = idescale(tmp0 + tmp7, 5);
        o[7] = idescale(tmp0 - tmp7, 5);
        o[1] = idescale(tmp1 + tmp6, 5);
        o[6] = idescale(tmp1 - tmp6, 5);
        o[2] = idescale(tmp2 + tmp5, 5);
        o[5] = idescale(tmp2 - tmp5, 5);
        o[4] = idescale(tmp3 + tmp4, 5);
        o[3] = idescale(tmp3 - tmp4, 5);
    }
}

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

    // Pre-scale each referenced dequant table by the AAN factors, in natural
    // order, so the fast IDCT applies dequant + scaling in one multiply.
    for (int i = 0; i < ncomp_; i++) {
        int tq = comp_[i].tq;
        for (int k = 0; k < 64; k++) {
            int nat = kZigzag[k];
            aan_qt_[tq][nat] = static_cast<int>(
                std::lround(qt_[tq][k] * kAanScale[nat >> 3] * kAanScale[nat & 7] * 4.0));
        }
    }

    PROF_SET(fmt, "jpeg");
    PROF_SET(src_w, w_);
    PROF_SET(src_h, h_);
    PROF_SET(scale, scale_);
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

void JpegDecoder::idct_reduce(const int *coeff, const int *q, uint8_t *dst,
                             int dst_stride, bool ac) {
    // DC value (block average) = F00 / 8; q[0] folds in the AAN ×4 + level scale.
    if (blk_ == 1) {  // 1/8 scale: only the average is needed
        dst[0] = clamp8(((coeff[0] * q[0] + 16) >> 5) + 128);
        return;
    }
    if (!ac) {  // flat block: no AC -> every pixel is the DC value (skip the IDCT)
        int v = clamp8(((coeff[0] * q[0] + 16) >> 5) + 128);
        for (int oy = 0; oy < blk_; oy++)
            for (int ox = 0; ox < blk_; ox++) dst[oy * dst_stride + ox] = static_cast<uint8_t>(v);
        return;
    }

    int spatial[64];
    idct8x8_fast(coeff, q, spatial);
    if (blk_ == 8) {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                dst[y * dst_stride + x] = clamp8(spatial[y * 8 + x] + 128);
        return;
    }
    int f = 8 / blk_, area = f * f;  // box-reduce 8x8 -> blk_ x blk_
    for (int oy = 0; oy < blk_; oy++) {
        for (int ox = 0; ox < blk_; ox++) {
            int sum = 0;
            for (int yy = 0; yy < f; yy++)
                for (int xx = 0; xx < f; xx++)
                    sum += spatial[(oy * f + yy) * 8 + ox * f + xx];
            dst[oy * dst_stride + ox] = clamp8(sum / area + 128);
        }
    }
}

void JpegDecoder::decode_block(int ci, uint8_t *dst, int dst_stride) {
    PROF_T0(te);
    Comp &c = comp_[ci];
    int coeff[64];
    std::memset(coeff, 0, sizeof coeff);

    int t = huffdecode(dc_[c.td]);
    int diff = t ? receive_extend(t) : 0;
    c.dcpred += diff;
    coeff[0] = c.dcpred;  // raw; the AAN dequant table is applied inside the IDCT

    bool ac = false;
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
        coeff[kZigzag[k]] = receive_extend(s);
        ac = true;
        k++;
    }
    PROF_ADD(entropy_us, te);
    PROF_T0(ti);
    idct_reduce(coeff, aan_qt_[c.tq], dst, dst_stride, ac);
    PROF_ADD(idct_us, ti);
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
        PROF_T0(tp);
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
        PROF_ADD(post_us, tp);
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
