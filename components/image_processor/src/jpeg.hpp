/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdint>

#include "rowsource.hpp"
#include "stream.hpp"

namespace imgproc {

// Self-contained baseline JPEG decoder (SOF0, 8-bit, Huffman). Decodes one MCU
// row at a time into a band buffer and serves rows top-to-bottom. Downscales
// while decoding by a 1/1..1/8 factor chosen from the target geometry: each 8x8
// block is reduced to (8/S)x(8/S) (DC-only at 1/8), which bounds RAM and CPU for
// large source images. Supports grayscale + YCbCr with sampling factors <= 2x2
// (4:4:4 / 4:2:2 / 4:4:0 / 4:2:0) and restart intervals. Progressive, 16-bit,
// arithmetic-coded, and CMYK streams return UnsupportedFormat from open().
class JpegDecoder : public Decoder {
public:
    ~JpegDecoder() override;
    Status open(InputStream &in, const Options &opts) override;
    bool next_row(uint8_t *dst) override;

private:
    struct Huff {
        uint8_t bits[17];
        uint8_t vals[256];
        int mincode[17];
        int maxcode[17];  // -1 when no code of that length
        int valptr[17];
        bool defined = false;
    };
    struct Comp {
        int id = 0, h = 1, v = 1, tq = 0;  // sampling + quant table
        int td = 0, ta = 0;                // DC / AC Huffman tables (from SOS)
        int dcpred = 0;
    };

    // --- header parsing ---
    int  raw();              // next raw stream byte, or -1
    int  u16();              // big-endian 16-bit
    bool skip(int n);
    bool parse_dqt(int len);
    bool parse_dht(int len);
    Status parse_sof(int len);
    Status parse_sos(int len);
    Status setup(const Options &opts);

    // --- entropy decode ---
    int  read_data_byte();   // entropy byte with FF-stuffing / marker handling
    int  getbit();
    int  getbits(int n);
    int  receive_extend(int s);
    int  huffdecode(const Huff &h);
    void decode_block(int ci, uint8_t *dst, int dst_stride);
    void idct_reduce(const int *coeff, const int *q, uint8_t *dst, int dst_stride, bool ac);
    bool consume_restart();
    void decode_mcu_row();

    InputStream *in_ = nullptr;
    uint32_t alloc_caps_ = 0;

    uint16_t w_ = 0, h_ = 0;
    int  ncomp_ = 0;
    Comp comp_[3];
    int  hmax_ = 1, vmax_ = 1;
    int  out_ch_ = 1;

    uint16_t qt_[4][64];      // dequant tables (zigzag order)
    int      aan_qt_[4][64];  // AAN-prescaled dequant (natural order) for the fast IDCT
    Huff dc_[4];
    Huff ac_[4];
    int  restart_interval_ = 0;

    int  scale_ = 1;  // S
    int  blk_ = 8;    // B = 8 / S
    int  mcus_per_row_ = 0, mcu_rows_ = 0, mcu_row_idx_ = 0;

    uint8_t *band_ = nullptr;
    int  band_w_ = 0, band_h_ = 0;
    int  band_valid_rows_ = 0, band_row_ = 0;
    uint32_t out_row_ = 0;

    // bit reader
    int  bitbuf_ = 0, bitcnt_ = 0;
    bool marker_pending_ = false;
    int  marker_ = 0;
    uint32_t mcu_count_ = 0;

    uint8_t compbuf_[3][16 * 16];  // per-component samples for one MCU (<= 16x16)
    bool err_ = false;
};

}  // namespace imgproc
