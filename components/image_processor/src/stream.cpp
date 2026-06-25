/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "stream.hpp"

#include <cstring>

namespace imgproc {

size_t InputStream::read(void *dst, size_t n) {
    uint8_t *d = static_cast<uint8_t *>(dst);
    size_t total = 0;

    size_t buffered = pb_len_ - pb_pos_;
    if (buffered) {
        size_t k = buffered < n ? buffered : n;
        std::memcpy(d, pb_ + pb_pos_, k);
        pb_pos_ += k;
        d += k;
        total += k;
        n -= k;
    }
    if (n) total += raw_read(d, n);
    return total;
}

size_t InputStream::peek(void *dst, size_t n) {
    if (n > kPeekMax) n = kPeekMax;

    size_t buffered = pb_len_ - pb_pos_;
    if (buffered < n) {
        if (pb_pos_) {  // compact unconsumed bytes to the front
            std::memmove(pb_, pb_ + pb_pos_, buffered);
            pb_len_ = buffered;
            pb_pos_ = 0;
        }
        pb_len_ += raw_read(pb_ + pb_len_, kPeekMax - pb_len_);
        buffered = pb_len_ - pb_pos_;
    }

    size_t k = buffered < n ? buffered : n;
    std::memcpy(dst, pb_ + pb_pos_, k);
    return k;
}

size_t BufferInputStream::raw_read(void *dst, size_t n) {
    size_t avail = len_ - pos_;
    size_t k = avail < n ? avail : n;
    std::memcpy(dst, data_ + pos_, k);
    pos_ += k;
    return k;
}

size_t FileInputStream::raw_read(void *dst, size_t n) {
    return std::fread(dst, 1, n, fp_);
}

}  // namespace imgproc
