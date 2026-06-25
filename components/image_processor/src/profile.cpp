/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "profile.hpp"

#if IMGPROC_PROFILE

#include <cstdio>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <chrono>
#endif

namespace imgproc {

Prof g_prof = {};

int64_t prof_now_us() {
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

void prof_reset() { g_prof = Prof{}; }

void prof_report(int w, int h) {
    const Prof &p = g_prof;
    int64_t compute = p.decode_us - p.io_us;      // decode without I/O wait
    int64_t color_down = p.transform_us - p.dither_us;
    std::printf(
        "[imgproc] %dx%d  total=%lldus  decode=%lldus (compute=%lldus io=%lldus,%d calls,%ldKB)  "
        "color+down=%lldus  dither=%lldus\n",
        w, h, (long long)p.total_us, (long long)p.decode_us, (long long)compute,
        (long long)p.io_us, p.io_calls, p.io_bytes / 1024,
        (long long)color_down, (long long)p.dither_us);
}

}  // namespace imgproc

#endif  // IMGPROC_PROFILE
