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

using namespace imgproc;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void test_status_str() {
    CHECK(std::strcmp(status_str(Status::Ok), "Ok") == 0);
    CHECK(std::strcmp(status_str(Status::TooLarge), "TooLarge") == 0);
}

static void test_skeleton_returns_unsupported() {
    // Phase 0: decoders are not wired yet; entry points report the stub status.
    Options opts;
    Image img;
    const unsigned char garbage[] = {0x00, 0x01, 0x02, 0x03};
    CHECK(decode_buffer(garbage, sizeof(garbage), opts, img) == Status::UnsupportedFormat);
    CHECK(img.data == nullptr);
    CHECK(decode_file("/no/such/file.png", opts, img) == Status::UnsupportedFormat);
}

static void test_image_move_is_safe() {
    Image a;
    Image b = std::move(a);
    CHECK(b.data == nullptr);
}

int main() {
    test_status_str();
    test_skeleton_returns_unsupported();
    test_image_move_is_safe();

    if (g_failures == 0) {
        std::printf("image_processor: all tests passed\n");
        return 0;
    }
    std::printf("image_processor: %d failure(s)\n", g_failures);
    return 1;
}
