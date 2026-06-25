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
#include "pipeline.hpp"
#include "rowsource.hpp"
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

// ---- Pipeline (synthetic RowSource) -----------------------------------------

// Uniform-color source.
struct SolidSource : RowSource {
    uint8_t r_, g_, b_;
    int cur_ = 0;
    SolidSource(int w, int h, PixelKind k, uint8_t r, uint8_t g = 0, uint8_t b = 0)
        : r_(r), g_(g), b_(b) { width = w; height = h; kind = k; }
    bool next_row(uint8_t *dst) override {
        if (cur_ >= height) return false;
        for (int x = 0; x < width; x++) {
            if (channels() == 1) dst[x] = r_;
            else { dst[3 * x] = r_; dst[3 * x + 1] = g_; dst[3 * x + 2] = b_; }
        }
        cur_++;
        return true;
    }
};

// Gray source whose value depends only on the row (for vertical box tests).
struct RowGradientSource : RowSource {
    const uint8_t *row_vals_;  // height entries
    int cur_ = 0;
    RowGradientSource(int w, int h, const uint8_t *vals) : row_vals_(vals) {
        width = w; height = h; kind = PixelKind::Gray8;
    }
    bool next_row(uint8_t *dst) override {
        if (cur_ >= height) return false;
        std::memset(dst, row_vals_[cur_], width);
        cur_++;
        return true;
    }
};

// Reports more rows than it will deliver.
struct TruncatedSource : RowSource {
    int deliver_, cur_ = 0;
    TruncatedSource(int w, int h, int deliver) : deliver_(deliver) {
        width = w; height = h; kind = PixelKind::Gray8;
    }
    bool next_row(uint8_t *dst) override {
        if (cur_ >= deliver_) return false;
        std::memset(dst, 128, width);
        cur_++;
        return true;
    }
};

static Options base_opts(uint16_t tw, uint16_t th) {
    Options o;
    o.target_w = tw;
    o.target_h = th;
    o.fit = Fit::Stretch;
    o.dither = Dither::None;
    o.levels = 16;
    return o;
}

static double mean_l8(const Image &img) {
    double s = 0;
    size_t count = static_cast<size_t>(img.w) * img.h;
    for (size_t i = 0; i < count; i++) s += img.data[i];  // L8 stride == w
    return s / count;
}

static void test_pipeline_identity_solid() {
    SolidSource src(8, 8, PixelKind::Gray8, 136);  // sits on 16-level grid
    Options o = base_opts(8, 8);
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::Ok);
    CHECK(img.w == 8 && img.h == 8);
    CHECK(img.format == OutFormat::L8 && img.stride == 8 && img.levels == 16);
    bool uniform = true;
    for (size_t i = 0; i < 64; i++) uniform &= img.data[i] == img.data[0];
    CHECK(uniform);
    CHECK(img.data[0] == 136);  // level 8 -> 8*17
}

static void test_pipeline_linear_vs_perceptual_downsample() {
    // 2x1 column: white over black, averaged to a single pixel.
    const uint8_t rows[2] = {255, 0};

    Options perc = base_opts(1, 1);
    perc.linear_downsample = false;
    RowGradientSource s1(1, 2, rows);
    Image a;
    CHECK(run_pipeline(s1, perc, a) == Status::Ok);
    CHECK(a.w == 1 && a.h == 1);
    CHECK(a.data[0] >= 110 && a.data[0] <= 145);  // perceptual midpoint

    Options lin = base_opts(1, 1);
    lin.linear_downsample = true;
    RowGradientSource s2(1, 2, rows);
    Image b;
    CHECK(run_pipeline(s2, lin, b) == Status::Ok);
    CHECK(b.data[0] >= 170);  // linear average is photometrically brighter
}

static void test_pipeline_luma_ordering() {
    Options o = base_opts(1, 1);
    Image r, g, b;
    SolidSource sr(1, 1, PixelKind::RGB888, 255, 0, 0);
    SolidSource sg(1, 1, PixelKind::RGB888, 0, 255, 0);
    SolidSource sb(1, 1, PixelKind::RGB888, 0, 0, 255);
    CHECK(run_pipeline(sr, o, r) == Status::Ok);
    CHECK(run_pipeline(sg, o, g) == Status::Ok);
    CHECK(run_pipeline(sb, o, b) == Status::Ok);
    CHECK(g.data[0] > r.data[0]);  // Rec709: green brightest, blue darkest
    CHECK(r.data[0] > b.data[0]);
}

static void test_pipeline_error_diffusion_mean() {
    SolidSource src(32, 32, PixelKind::Gray8, 128);
    Options o = base_opts(32, 32);
    o.dither = Dither::FloydSteinberg;
    o.levels = 16;
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::Ok);
    // Only the two levels bracketing the input survive, and the mean is preserved.
    bool only_two = true;
    for (size_t i = 0; i < 32u * 32; i++) {
        only_two &= (img.data[i] == 119 || img.data[i] == 136);  // levels 7 and 8
    }
    CHECK(only_two);
    double m = mean_l8(img);
    CHECK(m >= 124.0 && m <= 132.0);
}

static void test_pipeline_bayer_two_level() {
    SolidSource src(8, 8, PixelKind::Gray8, 128);
    Options o = base_opts(8, 8);
    o.dither = Dither::Bayer8;
    o.levels = 2;
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::Ok);
    bool binary = true;
    for (size_t i = 0; i < 64; i++) binary &= (img.data[i] == 0 || img.data[i] == 255);
    CHECK(binary);
    double m = mean_l8(img);
    CHECK(m >= 96.0 && m <= 160.0);  // ~half on
}

static void test_pipeline_pack_i1() {
    SolidSource src(16, 2, PixelKind::Gray8, 230);  // bright -> level 1
    Options o = base_opts(16, 2);
    o.out = OutFormat::I1;
    o.levels = 2;
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::Ok);
    CHECK(img.format == OutFormat::I1 && img.stride == 2);
    CHECK(img.data[0] == 0xFF && img.data[1] == 0xFF);  // all bits set
}

static void test_pipeline_pack_i4() {
    SolidSource src(8, 2, PixelKind::Gray8, 136);  // level 8
    Options o = base_opts(8, 2);
    o.out = OutFormat::I4;
    o.levels = 16;
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::Ok);
    CHECK(img.format == OutFormat::I4 && img.stride == 4);
    CHECK(img.data[0] == 0x88);  // two pixels of level 8
}

static void test_pipeline_invert() {
    SolidSource src(4, 4, PixelKind::Gray8, 255);
    Options o = base_opts(4, 4);
    o.invert = true;
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::Ok);
    CHECK(img.data[0] == 0);  // white inverted to black
}

static void test_pipeline_truncated() {
    TruncatedSource src(4, 4, 2);  // claims 4 rows, delivers 2
    Options o = base_opts(4, 4);
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::Truncated);
    CHECK(img.data == nullptr);
}

static void test_pipeline_bad_args() {
    SolidSource src(8, 8, PixelKind::Gray8, 100);
    Options o = base_opts(0, 0);  // Stretch needs both dims
    Image img;
    CHECK(run_pipeline(src, o, img) == Status::BadArgument);
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

    test_pipeline_identity_solid();
    test_pipeline_linear_vs_perceptual_downsample();
    test_pipeline_luma_ordering();
    test_pipeline_error_diffusion_mean();
    test_pipeline_bayer_two_level();
    test_pipeline_pack_i1();
    test_pipeline_pack_i4();
    test_pipeline_invert();
    test_pipeline_truncated();
    test_pipeline_bad_args();

    if (g_failures == 0) {
        std::printf("image_processor: all tests passed\n");
        return 0;
    }
    std::printf("image_processor: %d failure(s)\n", g_failures);
    return 1;
}
