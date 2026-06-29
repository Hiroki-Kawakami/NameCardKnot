/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Host unit tests for image_processor — no ESP-IDF, no hardware.
 * Compiled and run by test/run.sh (g++). image_processor is pure orchestration
 * over image_framework, so these exercise the public API end-to-end: container
 * sniffing, decode -> recolor -> resize -> dither -> pack, the option surface,
 * and the async job. The per-stage internals (decoders, recolor, resize, dither,
 * pack) have their own white-box tests in esp-devkit/libs/image_framework/test.
 */

#include "image_processor.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

#include "png_fixtures.h"
#include "jpeg_fixtures.h"

using namespace imgproc;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool near(int a, int b, int tol) { return (a > b ? a - b : b - a) <= tol; }

static double mean_l8(const Image &img) {
    double s = 0;
    size_t count = static_cast<size_t>(img.w) * img.h;
    for (size_t i = 0; i < count; i++) s += img.data[i];  // L8 stride == w
    return s / count;
}

// ---- Status / Image ---------------------------------------------------------

static void test_status_str() {
    CHECK(std::strcmp(status_str(Status::Ok), "Ok") == 0);
    CHECK(std::strcmp(status_str(Status::TooLarge), "TooLarge") == 0);
}

static void test_image_move_transfers_ownership() {
    Image a;
    CHECK(decode_buffer(kPngRgb2x2, sizeof kPngRgb2x2, Options{}, a) == Status::Ok);
    CHECK(a.data != nullptr && a.w == 2 && a.h == 2);

    Image b = std::move(a);
    CHECK(b.data != nullptr && b.w == 2 && b.h == 2);
    CHECK(a.data == nullptr);  // moved-from is empty (no double free)
    // b frees on scope exit.
}

// ---- decode_* entry points --------------------------------------------------

static void test_entry_points() {
    Options opts;
    Image img;

    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    CHECK(decode_buffer(garbage, sizeof garbage, opts, img) == Status::UnsupportedFormat);

    // Recognized container, but only the signature: open() runs out of data.
    const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(decode_buffer(png_sig, sizeof png_sig, opts, img) == Status::Truncated);

    CHECK(decode_file(nullptr, opts, img) == Status::BadArgument);
    CHECK(decode_file("/no/such/file.png", opts, img) == Status::OpenFailed);
}

static void test_too_large_guard() {
    Options o;
    o.max_src_pixels = 8;  // kPngRgb2x2 is 4 px — fits; a 1px cap would reject
    Image img;
    CHECK(decode_buffer(kPngRgb2x2, sizeof kPngRgb2x2, o, img) == Status::Ok);
    o.max_src_pixels = 3;  // PNG ignores the decode-time hint, so 4 > 3 -> reject
    CHECK(decode_buffer(kPngRgb2x2, sizeof kPngRgb2x2, o, img) == Status::TooLarge);
}

// Decode a JPEG embedded inside a larger file (offset/length sub-range) — the
// .mnc.pdf display-image path.
static void test_file_subrange() {
    const char *path = "/tmp/imgproc_subrange.bin";
    const uint8_t pad[100] = {0};
    FILE *f = std::fopen(path, "wb");
    CHECK(f != nullptr);
    if (!f) return;
    std::fwrite(pad, 1, sizeof pad, f);
    std::fwrite(kJpgGrayBig, 1, sizeof kJpgGrayBig, f);
    std::fwrite(pad, 1, sizeof pad, f);
    std::fclose(f);

    Options o;
    Image img;
    CHECK(decode_file(path, o, img) != Status::Ok);  // leading padding isn't an image
    Status st = decode_file(path, o, img, nullptr, (uint32_t)sizeof pad, (uint32_t)sizeof kJpgGrayBig);
    CHECK(st == Status::Ok);
    CHECK(img.w == 64 && img.h == 64);
    std::remove(path);
}

// ---- PNG end-to-end ---------------------------------------------------------

static void test_png_default_size() {
    // No target -> Contain at source size; output is L8.
    Image img;
    CHECK(decode_buffer(kPngRgb2x2, sizeof kPngRgb2x2, Options{}, img) == Status::Ok);
    CHECK(img.w == 2 && img.h == 2);
    CHECK(img.format == OutFormat::L8 && img.stride == 2 && img.levels == 16);
}

static void test_png_luma_ordering() {
    // Fixture layout: (0,0) red, (1,0) green, (0,1) blue, (1,1) white.
    Options o;
    o.dither = Dither::None;
    Image img;
    CHECK(decode_buffer(kPngRgb2x2, sizeof kPngRgb2x2, o, img) == Status::Ok);
    uint8_t r = img.data[0], g = img.data[1], b = img.data[2], w = img.data[3];
    CHECK(g > r && r > b);   // Rec709: green brightest, blue darkest of the primaries
    CHECK(w >= g);           // white is the brightest overall
}

static void test_png_gradient_contain() {
    // 16x16 gradient downscaled to 8x8 stays monotonic-ish and mid-toned.
    Options o;
    o.target_w = 8;
    o.target_h = 8;
    o.dither = Dither::None;
    Image img;
    CHECK(decode_buffer(kPngGrad16, sizeof kPngGrad16, o, img) == Status::Ok);
    CHECK(img.w == 8 && img.h == 8);
    double m = mean_l8(img);
    CHECK(m > 20.0 && m < 235.0);
}

// ---- JPEG end-to-end --------------------------------------------------------

static void test_jpeg_flat_pipeline() {
    Options o;
    o.target_w = 16;
    o.target_h = 16;
    o.fit = Fit::Stretch;
    o.dither = Dither::None;
    Image img;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, o, img) == Status::Ok);
    CHECK(img.w == 16 && img.h == 16);
    CHECK(near(img.data[0], 136, 20));  // ~128 quantized to the nearest of 16 levels
}

static void test_jpeg_decode_time_downscale() {
    // 64x64 source, target 16x16 -> the JPEG decoder picks a 1/N factor.
    Options o;
    o.target_w = 16;
    o.target_h = 16;
    o.dither = Dither::None;
    Image img;
    CHECK(decode_buffer(kJpgGrayBig, sizeof kJpgGrayBig, o, img) == Status::Ok);
    CHECK(img.w == 16 && img.h == 16);
    CHECK(near(img.data[0], 100, 20));
}

// ---- Option surface (uniform JPEG-flat source) ------------------------------

static Options flat_opts(OutFormat out, uint8_t levels, Dither d) {
    Options o;
    o.target_w = 8;
    o.target_h = 2;
    o.fit = Fit::Stretch;
    o.dither = d;
    o.levels = levels;
    o.out = out;
    return o;
}

static void test_invert() {
    Options o = flat_opts(OutFormat::L8, 16, Dither::None);
    o.invert = true;
    Image img;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, o, img) == Status::Ok);
    CHECK(near(img.data[0], 255 - 136, 20));  // the ~136 gray, inverted
}

static void test_pack_i4() {
    Image img;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, flat_opts(OutFormat::I4, 16, Dither::None),
                        img) == Status::Ok);
    CHECK(img.format == OutFormat::I4 && img.stride == 4);
    CHECK(img.data[0] == 0x88);  // two pixels of level 8 (~128)
}

static void test_pack_i1() {
    Image img;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, flat_opts(OutFormat::I1, 2, Dither::Bayer8),
                        img) == Status::Ok);
    CHECK(img.format == OutFormat::I1 && img.stride == 1);
    // ~128 dithered to 2 levels over an 8-wide row: a mix of set/clear bits.
    CHECK(img.data[0] != 0x00 || img.data[1] != 0x00);
}

static void test_levels_two_binary() {
    Options o = flat_opts(OutFormat::L8, 2, Dither::Bayer8);
    o.target_w = 16;
    o.target_h = 16;
    Image img;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, o, img) == Status::Ok);
    bool binary = true;
    for (size_t i = 0; i < 16u * 16; i++) binary &= (img.data[i] == 0 || img.data[i] == 255);
    CHECK(binary);
    double m = mean_l8(img);
    CHECK(m >= 96.0 && m <= 176.0);  // ~half on for a mid-gray
}

static void test_error_diffusion_brackets() {
    Options o = flat_opts(OutFormat::L8, 16, Dither::FloydSteinberg);
    o.target_w = 32;
    o.target_h = 32;
    Image img;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, o, img) == Status::Ok);
    // A near-uniform mid-gray dithers to the two levels bracketing it (7/8).
    bool only_two = true;
    for (size_t i = 0; i < 32u * 32; i++) only_two &= (img.data[i] == 119 || img.data[i] == 136);
    CHECK(only_two);
    CHECK(near((int)mean_l8(img), 128, 12));
}

// ---- Async + progress -------------------------------------------------------

// Polls to a terminal state — works for both the synchronous (IMGPROC_ASYNC=0)
// and the real two-task (IMGPROC_ASYNC=1, pthreads) builds.
static DecodeJob::State wait_done(DecodeJob &j) {
    for (long i = 0; i < 100000000L; i++) {
        DecodeJob::State s = j.state();
        if (s != DecodeJob::State::Running) return s;
    }
    return j.state();
}

static void test_async_job() {
    Options o;
    o.target_w = 2;
    o.target_h = 2;
    o.fit = Fit::Stretch;
    o.dither = Dither::None;

    // Bad path -> Failed (a failed open is reported before the tasks start).
    auto bad = decode_file_async("/no/such/file.png", o);
    CHECK(wait_done(*bad) == DecodeJob::State::Failed);
    CHECK(bad->status() == Status::OpenFailed);
    CHECK(bad->progress_pct() == 0);

    const char *path = "/tmp/imgproc_async_test.png";
    FILE *f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(kPngRgb2x2, 1, sizeof kPngRgb2x2, f);
        std::fclose(f);
        auto job = decode_file_async(path, o);
        CHECK(wait_done(*job) == DecodeJob::State::Ok);
        CHECK(job->progress_pct() == 100);
        Image img = job->take_image();
        CHECK(img.w == 2 && img.h == 2 && img.data != nullptr);
        std::remove(path);
    }

    // Drop a job handle mid-flight: the dtor must cancel + join cleanly (no leak,
    // no use-after-free of the job's Progress by the worker tasks).
    f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(kPngRgb2x2, 1, sizeof kPngRgb2x2, f);
        std::fclose(f);
        { auto throwaway = decode_file_async(path, o); }  // dtor runs here
        std::remove(path);
    }
}

static void test_progress_and_cancel() {
    Options o;
    o.target_w = 16;
    o.target_h = 16;
    o.fit = Fit::Stretch;
    o.dither = Dither::None;

    Progress prog;
    Image img;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, o, img, &prog) == Status::Ok);
    CHECK(prog.total.load() == 16);
    CHECK(prog.done.load() == 16);  // reaches total on success

    Progress prog2;
    prog2.cancel.store(true);  // pre-cancelled -> aborts before producing output
    Image img2;
    CHECK(decode_buffer(kJpgGrayFlat, sizeof kJpgGrayFlat, o, img2, &prog2) == Status::Cancelled);
    CHECK(img2.data == nullptr);
}

int main() {
    test_status_str();
    test_image_move_transfers_ownership();
    test_entry_points();
    test_too_large_guard();
    test_file_subrange();

    test_png_default_size();
    test_png_luma_ordering();
    test_png_gradient_contain();

    test_jpeg_flat_pipeline();
    test_jpeg_decode_time_downscale();

    test_invert();
    test_pack_i4();
    test_pack_i1();
    test_levels_two_binary();
    test_error_diffusion_brackets();

    test_async_job();
    test_progress_and_cancel();

    if (g_failures == 0) {
        std::printf("image_processor: all tests passed\n");
        return 0;
    }
    std::printf("image_processor: %d failure(s)\n", g_failures);
    return 1;
}
