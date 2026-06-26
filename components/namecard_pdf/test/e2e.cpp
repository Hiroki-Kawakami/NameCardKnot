/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * End-to-end: parse a container PDF, pull out an embedded JPEG, and decode it
 * through image_processor — the real device path. Host-only (g++, no ESP-IDF).
 * Run via run_e2e.sh.
 */

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "image_processor.hpp"
#include "namecard_pdf.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("FAIL: %s\n", msg);                              \
            failures++;                                             \
        }                                                           \
    } while (0)

static std::vector<uint8_t> read_file(const std::string &p) {
    std::vector<uint8_t> out;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    if (n > 0 && fread(out.data(), 1, n, f) != (size_t)n) out.clear();
    fclose(f);
    return out;
}

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "fixtures";
    auto buf = read_file(dir + "/basic.mnc.pdf");
    CHECK(!buf.empty(), "read fixture");

    nckpdf::Card card;
    CHECK(nckpdf::parse_buffer(buf.data(), buf.size(), card) == nckpdf::Status::Ok, "parse");

    // Decode the display image at the EPD resolution, like the device does.
    const nckpdf::Asset *disp = card.find(nckpdf::AssetType::DisplayJpeg);
    CHECK(disp != nullptr, "display present");
    if (disp) {
        imgproc::Options opts;
        opts.target_w = 540;
        opts.target_h = 960;
        imgproc::Image img;
        imgproc::Status st = imgproc::decode_buffer(buf.data() + disp->offset, disp->length, opts, img);
        CHECK(st == imgproc::Status::Ok, "decode display");
        printf("  decoded display -> %ux%u (status=%s)\n", img.w, img.h, imgproc::status_str(st));
        CHECK(img.w > 0 && img.h > 0 && img.data != nullptr, "decoded dims");
    }

    // Decode the first share image too (grayscale JPEG path).
    auto shares = card.all(nckpdf::AssetType::ShareJpeg);
    CHECK(!shares.empty(), "share present");
    if (!shares.empty()) {
        imgproc::Options opts;
        opts.target_w = 400;
        imgproc::Image img;
        imgproc::Status st =
            imgproc::decode_buffer(buf.data() + shares[0]->offset, shares[0]->length, opts, img);
        CHECK(st == imgproc::Status::Ok, "decode share");
        printf("  decoded share   -> %ux%u (status=%s)\n", img.w, img.h, imgproc::status_str(st));
    }

    if (failures) {
        printf("\n%d E2E CHECK(s) failed\n", failures);
        return 1;
    }
    printf("namecard_pdf -> image_processor E2E passed\n");
    return 0;
}
