/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdint>

// Lightweight per-decode stage timing. On by default (device + simulator);
// the host unit tests build with -DIMGPROC_PROFILE=0 to stay quiet. Output goes
// to printf (device: idf monitor; simulator: terminal).
#ifndef IMGPROC_PROFILE
#define IMGPROC_PROFILE 1
#endif

#if IMGPROC_PROFILE
namespace imgproc {

struct Prof {
    int64_t total_us;      // whole decode_stream
    int64_t decode_us;     // RowSource::next_row (decode compute + I/O)
    int64_t io_us;         // raw source reads (SD / file)
    int64_t transform_us;  // color convert + downsample (includes dither_us)
    int64_t dither_us;     // finalize + dither + pack
    int     io_calls;
    long    io_bytes;
};

extern Prof g_prof;
int64_t prof_now_us();
void prof_reset();
void prof_report(int w, int h);

}  // namespace imgproc

#define PROF_RESET()        ::imgproc::prof_reset()
#define PROF_T0(v)          int64_t v = ::imgproc::prof_now_us()
#define PROF_ADD(field, t0) (::imgproc::g_prof.field += ::imgproc::prof_now_us() - (t0))
#define PROF_REPORT(w, h)   ::imgproc::prof_report((w), (h))
#else
#define PROF_RESET()        ((void)0)
#define PROF_T0(v)          ((void)0)
#define PROF_ADD(field, t0) ((void)0)
#define PROF_REPORT(w, h)   ((void)0)
#endif
