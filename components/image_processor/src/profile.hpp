/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdint>

// Lightweight per-decode stage timing. On by default (device + simulator);
// the host unit tests build with -DIMGPROC_PROFILE=0 to stay quiet. Output goes
// to printf (device: idf monitor; simulator: terminal). The per-stage internals
// of decode/resize/dither now live in image_framework, so this only times the
// orchestration boundaries.
#ifndef IMGPROC_PROFILE
#define IMGPROC_PROFILE 1
#endif

#if IMGPROC_PROFILE
namespace imgproc {

struct Prof {
    int64_t total_us;      // whole decode
    int64_t decode_us;     // imgf_decoder_next_row (decode + I/O)
    int64_t transform_us;  // recolor + resize (includes dither_us)
    int64_t dither_us;     // finalize + dither + pack
    const char *fmt;       // "png" / "jpeg"
    int     src_w, src_h;
};

extern Prof g_prof;
int64_t prof_now_us();
void prof_reset();
void prof_report(int w, int h);

}  // namespace imgproc

#define PROF_RESET()        ::imgproc::prof_reset()
#define PROF_T0(v)          int64_t v = ::imgproc::prof_now_us()
#define PROF_ADD(field, t0) (::imgproc::g_prof.field += ::imgproc::prof_now_us() - (t0))
#define PROF_SET(field, v)  (::imgproc::g_prof.field = (v))
#define PROF_REPORT(w, h)   ::imgproc::prof_report((w), (h))
#else
#define PROF_RESET()        ((void)0)
#define PROF_T0(v)          ((void)0)
#define PROF_ADD(field, t0) ((void)0)
#define PROF_SET(field, v)  ((void)0)
#define PROF_REPORT(w, h)   ((void)0)
#endif
