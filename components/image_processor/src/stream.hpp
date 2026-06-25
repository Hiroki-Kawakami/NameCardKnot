/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Sequential byte source for decoders. read() consumes; peek() looks ahead
// without consuming (used for format sniffing) up to kPeekMax bytes. No seek:
// decoders are single-pass over the stream.
//
// The decoders pull one byte at a time, so the base class batches the source
// into a kBufSize window: raw_read() (the per-source fetch) is only called per
// buffer refill, never per byte. This matters on the device, where a 1-byte
// fread from FAT-over-SDSPI is very slow.
namespace imgproc {

class InputStream {
public:
    static constexpr size_t kBufSize = 4096;  // source reads are batched to this
    static constexpr size_t kPeekMax = 16;    // max non-consuming look-ahead

    virtual ~InputStream() = default;

    // Returns bytes actually copied (< n only at end of stream).
    size_t read(void *dst, size_t n);
    // Non-consuming look-ahead; n is clamped to kPeekMax.
    size_t peek(void *dst, size_t n);

protected:
    virtual size_t raw_read(void *dst, size_t n) = 0;

private:
    uint8_t buf_[kBufSize];
    size_t  buf_len_ = 0;  // valid bytes in buf_
    size_t  buf_pos_ = 0;  // bytes already consumed from buf_
};

class BufferInputStream : public InputStream {
public:
    BufferInputStream(const void *data, size_t len)
        : data_(static_cast<const uint8_t *>(data)), len_(len) {}

protected:
    size_t raw_read(void *dst, size_t n) override;

private:
    const uint8_t *data_;
    size_t len_;
    size_t pos_ = 0;
};

// Does not own the FILE*; the caller opens and closes it.
class FileInputStream : public InputStream {
public:
    explicit FileInputStream(FILE *fp) : fp_(fp) {}

protected:
    size_t raw_read(void *dst, size_t n) override;

private:
    FILE *fp_;
};

}  // namespace imgproc
