/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "inflate.hpp"

#include <new>

namespace imgproc {

static constexpr uint32_t kWinSize = 32768;
static constexpr uint32_t kWinMask = kWinSize - 1;

// RFC 1951 length / distance code tables.
static const uint16_t kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint8_t kClOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

int Inflate::next_byte() {
    if (in_avail_ == 0) {
        in_ptr_ = src_->refill(&in_avail_);
        if (in_avail_ == 0) { eof_ = true; return -1; }
    }
    in_avail_--;
    return *in_ptr_++;
}

uint32_t Inflate::bits(int need) {
    uint32_t val = bitbuf_;
    while (bitcnt_ < need) {
        int c = next_byte();
        if (c < 0) c = 0;
        val |= static_cast<uint32_t>(c) << bitcnt_;
        bitcnt_ += 8;
    }
    bitbuf_ = val >> need;
    bitcnt_ -= need;
    return val & ((1u << need) - 1);
}

static int reverse_bits(int c, int len) {
    int r = 0;
    for (int i = 0; i < len; i++) { r = (r << 1) | (c & 1); c >>= 1; }
    return r;
}

// Canonical bit-by-bit walk; used only for codes longer than kFastBits.
int Inflate::decode_slow(const Huff &h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= static_cast<int>(bits(1));
        int count = h.count[len];
        if (code - first < count) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

int Inflate::decode(const Huff &h) {
    while (bitcnt_ < kFastBits) {  // peek kFastBits without consuming
        int c = next_byte();
        if (c < 0) c = 0;
        bitbuf_ |= static_cast<uint32_t>(c) << bitcnt_;
        bitcnt_ += 8;
    }
    uint16_t e = h.fast[bitbuf_ & (kFastSize - 1)];
    if (e) {
        int len = e >> 9;
        bitbuf_ >>= len;
        bitcnt_ -= len;
        return e & 0x1FF;
    }
    return decode_slow(h);  // code longer than kFastBits
}

int Inflate::construct(Huff &h, const uint8_t *lengths, int n) {
    for (int i = 0; i < 16; i++) h.count[i] = 0;
    for (int s = 0; s < n; s++) h.count[lengths[s]]++;
    for (int i = 0; i < kFastSize; i++) h.fast[i] = 0;
    if (h.count[0] == n) return 0;  // no codes (valid for an empty distance tree)

    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;  // over-subscribed
    }

    int16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int s = 0; s < n; s++)
        if (lengths[s]) h.symbol[offs[lengths[s]]++] = static_cast<int16_t>(s);

    // Fast table: assign canonical codes, fill all entries whose low `len` bits
    // (LSB-first, so the code is bit-reversed) match a code of length <= kFastBits.
    uint16_t next_code[16];
    int code = 0;
    for (int len = 1; len <= 15; len++) { next_code[len] = static_cast<uint16_t>(code); code = (code + h.count[len]) << 1; }
    for (int s = 0; s < n; s++) {
        int len = lengths[s];
        if (!len) continue;
        int c = next_code[len]++;
        if (len <= kFastBits) {
            int rev = reverse_bits(c, len);
            for (int j = rev; j < kFastSize; j += (1 << len))
                h.fast[j] = static_cast<uint16_t>((len << 9) | s);
        }
    }
    return left;  // > 0 => incomplete
}

void Inflate::build_fixed() {
    uint8_t lengths[288];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    construct(lit_, lengths, 288);

    uint8_t dlen[30];
    for (i = 0; i < 30; i++) dlen[i] = 5;
    construct(dist_, dlen, 30);
}

bool Inflate::build_dynamic() {
    int hlit = static_cast<int>(bits(5)) + 257;
    int hdist = static_cast<int>(bits(5)) + 1;
    int hclen = static_cast<int>(bits(4)) + 4;
    if (hlit > 286 || hdist > 30) return false;

    uint8_t cl_lengths[19] = {0};
    for (int i = 0; i < hclen; i++) cl_lengths[kClOrder[i]] = static_cast<uint8_t>(bits(3));
    Huff cl;
    if (construct(cl, cl_lengths, 19) < 0) return false;

    uint8_t lengths[286 + 30] = {0};
    int n = hlit + hdist;
    int idx = 0;
    while (idx < n) {
        int sym = decode(cl);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[idx++] = static_cast<uint8_t>(sym);
        } else if (sym == 16) {
            if (idx == 0) return false;
            int rep = 3 + static_cast<int>(bits(2));
            uint8_t prev = lengths[idx - 1];
            while (rep-- && idx < n) lengths[idx++] = prev;
        } else if (sym == 17) {
            int rep = 3 + static_cast<int>(bits(3));
            while (rep-- && idx < n) lengths[idx++] = 0;
        } else {  // 18
            int rep = 11 + static_cast<int>(bits(7));
            while (rep-- && idx < n) lengths[idx++] = 0;
        }
    }
    if (idx != n) return false;
    if (construct(lit_, lengths, hlit) < 0) return false;
    if (construct(dist_, lengths + hlit, hdist) < 0) return false;
    return true;
}

bool Inflate::init(ByteSource *src) {
    src_ = src;
    win_.reset(new (std::nothrow) uint8_t[kWinSize]);
    if (!win_) return false;

    int cmf = next_byte();
    int flg = next_byte();
    if (cmf < 0 || flg < 0) return false;
    if ((cmf & 0x0F) != 8) return false;          // not DEFLATE
    if (((cmf << 8) | flg) % 31 != 0) return false;  // header checksum
    if (flg & 0x20) return false;                 // preset dictionary: unsupported
    return true;
}

size_t Inflate::read(uint8_t *out, size_t n) {
    size_t produced = 0;
    while (produced < n && !ended_ && !err_) {
        if (!block_active_) {
            bfinal_ = static_cast<int>(bits(1));
            int bt = static_cast<int>(bits(2));
            if (eof_) { ended_ = true; break; }
            if (bt == 0) {
                bitbuf_ = 0;
                bitcnt_ = 0;  // align to byte boundary
                int l0 = next_byte(), l1 = next_byte();
                next_byte();  // NLEN (ignored)
                next_byte();
                if (l0 < 0 || l1 < 0) { ended_ = true; break; }
                stored_rem_ = static_cast<uint32_t>(l0 | (l1 << 8));
                mode_ = 0;
            } else if (bt == 1) {
                build_fixed();
                mode_ = 1;
                copy_rem_ = 0;
            } else if (bt == 2) {
                if (!build_dynamic()) { err_ = true; break; }
                mode_ = 1;
                copy_rem_ = 0;
            } else {
                err_ = true;
                break;
            }
            block_active_ = true;
        }

        if (mode_ == 0) {
            while (produced < n && stored_rem_ > 0) {
                int c = next_byte();
                if (c < 0) { err_ = true; break; }
                uint8_t b = static_cast<uint8_t>(c);
                out[produced++] = b;
                win_[wpos_] = b;
                wpos_ = (wpos_ + 1) & kWinMask;
                stored_rem_--;
            }
            if (stored_rem_ == 0) {
                block_active_ = false;
                if (bfinal_) ended_ = true;
            }
            continue;
        }

        while (produced < n) {
            if (copy_rem_ > 0) {
                while (produced < n && copy_rem_ > 0) {
                    uint8_t b = win_[(wpos_ - copy_dist_) & kWinMask];
                    out[produced++] = b;
                    win_[wpos_] = b;
                    wpos_ = (wpos_ + 1) & kWinMask;
                    copy_rem_--;
                }
                if (copy_rem_ > 0) break;  // output full; resume copy next call
                continue;  // copy done; re-check produced < n before decoding more
            }

            int sym = decode(lit_);
            if (sym < 0) { err_ = true; break; }
            if (sym < 256) {
                uint8_t b = static_cast<uint8_t>(sym);
                out[produced++] = b;
                win_[wpos_] = b;
                wpos_ = (wpos_ + 1) & kWinMask;
            } else if (sym == 256) {
                block_active_ = false;
                if (bfinal_) ended_ = true;
                break;
            } else {
                sym -= 257;
                if (sym >= 29) { err_ = true; break; }
                int len = kLenBase[sym] + static_cast<int>(bits(kLenExtra[sym]));
                int ds = decode(dist_);
                if (ds < 0 || ds >= 30) { err_ = true; break; }
                int dist = kDistBase[ds] + static_cast<int>(bits(kDistExtra[ds]));
                copy_dist_ = dist;
                copy_rem_ = len;
            }
        }
    }
    return produced;
}

}  // namespace imgproc
