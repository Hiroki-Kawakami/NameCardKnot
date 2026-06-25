/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Host unit tests for image_processor — no ESP-IDF, no hardware.
 * Compiled and run by test/run.sh (g++). Each phase adds cases here.
 */

#include "image_processor.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

// Internal headers (test/run.sh adds -I src) — white-box coverage.
#include "alloc.hpp"
#include "sniff.hpp"
#include "stream.hpp"

using namespace imgproc;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

// ---- Status / Image ---------------------------------------------------------

static void test_status_str() {
    CHECK(std::strcmp(status_str(Status::Ok), "Ok") == 0);
    CHECK(std::strcmp(status_str(Status::TooLarge), "TooLarge") == 0);
}

static void test_alloc_roundtrip() {
    void *p = img_alloc(256, 0);
    CHECK(p != nullptr);
    std::memset(p, 0xAB, 256);
    img_free(p);
    img_free(nullptr);  // must be safe
}

static void test_image_move_transfers_ownership() {
    Image a;
    a.data = static_cast<uint8_t *>(img_alloc(64, 0));
    a.w = 8;
    a.h = 8;
    a.stride = 8;
    CHECK(a.data != nullptr);

    Image b = std::move(a);
    CHECK(b.data != nullptr);
    CHECK(b.w == 8 && b.h == 8);
    CHECK(a.data == nullptr);  // moved-from is empty (no double free)
    // b frees on scope exit.
}

// ---- InputStream ------------------------------------------------------------

static void test_buffer_stream_read() {
    const uint8_t src[] = {1, 2, 3, 4, 5};
    BufferInputStream in(src, sizeof src);
    uint8_t buf[3];
    CHECK(in.read(buf, 3) == 3);
    CHECK(buf[0] == 1 && buf[2] == 3);
    CHECK(in.read(buf, 3) == 2);  // only 2 left
    CHECK(buf[0] == 4 && buf[1] == 5);
    CHECK(in.read(buf, 3) == 0);  // EOF
}

static void test_peek_is_nonconsuming() {
    const uint8_t src[] = {0x10, 0x20, 0x30, 0x40};
    BufferInputStream in(src, sizeof src);
    uint8_t a[4], b[4];
    CHECK(in.peek(a, 4) == 4);
    CHECK(in.peek(b, 4) == 4);
    CHECK(std::memcmp(a, b, 4) == 0);  // peek does not advance
    uint8_t r[4];
    CHECK(in.read(r, 4) == 4);
    CHECK(std::memcmp(a, r, 4) == 0);  // read sees the peeked bytes
}

static void test_peek_then_partial_read() {
    const uint8_t src[] = {9, 8, 7, 6, 5};
    BufferInputStream in(src, sizeof src);
    uint8_t hdr[2];
    CHECK(in.peek(hdr, 2) == 2);
    uint8_t all[5];
    CHECK(in.read(all, 5) == 5);
    CHECK(all[0] == 9 && all[4] == 5);  // peeked bytes are still delivered
}

static void test_file_stream() {
    FILE *fp = std::tmpfile();
    CHECK(fp != nullptr);
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::fwrite(payload, 1, sizeof payload, fp);
    std::rewind(fp);

    FileInputStream in(fp);
    uint8_t got[4];
    CHECK(in.peek(got, 2) == 2);
    CHECK(got[0] == 0xDE && got[1] == 0xAD);
    CHECK(in.read(got, 4) == 4);
    CHECK(got[3] == 0xEF);
    std::fclose(fp);
}

// ---- sniff / size guard -----------------------------------------------------

static void test_sniff() {
    const uint8_t png[8]  = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    const uint8_t jpeg[3] = {0xFF, 0xD8, 0xFF};
    const uint8_t junk[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    CHECK(sniff(png, 8) == Format::Png);
    CHECK(sniff(jpeg, 3) == Format::Jpeg);
    CHECK(sniff(junk, 8) == Format::Unknown);
    CHECK(sniff(png, 4) == Format::Unknown);  // too few bytes
}

static void test_check_src_size() {
    CHECK(check_src_size(100, 100, 0) == Status::Ok);            // cap disabled
    CHECK(check_src_size(100, 100, 20000) == Status::Ok);
    CHECK(check_src_size(100, 100, 5000) == Status::TooLarge);   // 10000 > 5000
    CHECK(check_src_size(0, 100, 0) == Status::DecodeError);     // zero dim
}

// ---- decode_* entry points --------------------------------------------------

static void test_entry_points() {
    Options opts;
    Image img;

    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    CHECK(decode_buffer(garbage, sizeof garbage, opts, img) == Status::UnsupportedFormat);

    // Recognized container, but the decoder is not wired until Phase 3/4.
    const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(decode_buffer(png_sig, sizeof png_sig, opts, img) == Status::UnsupportedFormat);

    CHECK(decode_file(nullptr, opts, img) == Status::BadArgument);
    CHECK(decode_file("/no/such/file.png", opts, img) == Status::OpenFailed);
}

int main() {
    test_status_str();
    test_alloc_roundtrip();
    test_image_move_transfers_ownership();
    test_buffer_stream_read();
    test_peek_is_nonconsuming();
    test_peek_then_partial_read();
    test_file_stream();
    test_sniff();
    test_check_src_size();
    test_entry_points();

    if (g_failures == 0) {
        std::printf("image_processor: all tests passed\n");
        return 0;
    }
    std::printf("image_processor: %d failure(s)\n", g_failures);
    return 1;
}
