/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "stream.hpp"

#include <cstring>

#include "profile.hpp"

namespace imgproc {

size_t InputStream::source_read(void *dst, size_t n) {
#if IMGPROC_PROFILE
    int64_t t = prof_now_us();
    size_t got = raw_read(dst, n);
    g_prof.io_us += prof_now_us() - t;
    g_prof.io_calls++;
    g_prof.io_bytes += static_cast<long>(got);
    return got;
#else
    return raw_read(dst, n);
#endif
}

size_t InputStream::read(void *dst, size_t n) {
    uint8_t *d = static_cast<uint8_t *>(dst);
    size_t total = 0;

    while (n) {
        size_t avail = buf_len_ - buf_pos_;
        if (avail == 0) {  // refill the window from the source
            buf_pos_ = 0;
            buf_len_ = source_read(buf_, kBufSize);
            if (buf_len_ == 0) break;  // end of stream
            avail = buf_len_;
        }
        size_t k = avail < n ? avail : n;
        std::memcpy(d, buf_ + buf_pos_, k);
        buf_pos_ += k;
        d += k;
        total += k;
        n -= k;
    }
    return total;
}

size_t InputStream::peek(void *dst, size_t n) {
    if (n > kPeekMax) n = kPeekMax;

    size_t avail = buf_len_ - buf_pos_;
    if (avail < n) {
        if (buf_pos_) {  // compact unconsumed bytes to the front
            std::memmove(buf_, buf_ + buf_pos_, avail);
            buf_len_ = avail;
            buf_pos_ = 0;
        }
        while (buf_len_ < n) {  // top up enough for the look-ahead
            size_t got = source_read(buf_ + buf_len_, kBufSize - buf_len_);
            if (got == 0) break;
            buf_len_ += got;
        }
        avail = buf_len_ - buf_pos_;
    }

    size_t k = avail < n ? avail : n;
    std::memcpy(dst, buf_ + buf_pos_, k);
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
