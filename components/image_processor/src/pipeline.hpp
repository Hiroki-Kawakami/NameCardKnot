/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include "image_processor.hpp"
#include "rowsource.hpp"

namespace imgproc {

// Dual-core split: decode (producer) stays on the calling task while color +
// dither (consumer) run on a second task created with these settings. The
// decode is the "heavy", input-dependent side, so it keeps the caller's
// task_priority/task_core (see decode_file_async); the consumer takes the other
// core. Ignored without FreeRTOS (host unit tests) — the pipeline runs serially.
struct ParallelCfg {
    int consumer_core;
    int consumer_prio;
};

// Streams `src` top-to-bottom through downscale -> color -> dither -> pack and
// fills `out` (allocated here). Holds only per-row working buffers plus the
// output, never the full-resolution source. Decoder-agnostic: also driven
// directly by the host tests with a synthetic RowSource. With `par` non-null the
// decode and color+dither halves run on two tasks (one row-buffer ring between).
Status run_pipeline(RowSource &src, const Options &opts, Image &out, Progress *prog = nullptr,
                    const ParallelCfg *par = nullptr);

// Like decode_file but runs the pipeline across two tasks (see ParallelCfg).
// Internal entry used by the async DecodeJob; the public decode_file is serial.
Status decode_file_parallel(const char *path, const Options &opts, Image &out, Progress *prog,
                            const ParallelCfg &par);

}  // namespace imgproc
