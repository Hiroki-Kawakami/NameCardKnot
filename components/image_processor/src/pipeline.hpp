/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstdio>

#include "image_processor.hpp"
#include "imgf_decoder.h"
#include "imgf_stream.h"
#include "imgf_types.h"

namespace imgproc {

// Map an image_framework error to the public Status.
Status status_from_imgf(imgf_err_t e);

// Streams the opened `dec` top-to-bottom through recolor -> downscale (imgf
// resizer) -> recolor finalize -> dither -> pack and fills `out` (allocated
// here). Serial: runs on the calling task. Only per-row working buffers plus the
// output are held.
Status run_pipeline(imgf_decoder_t *dec, const Options &opts, Image &out, Progress *prog = nullptr);

// Open + sniff a decoder for a file sub-range and apply the source-size guard.
// `fs` is caller-owned storage the decoder references (must outlive it). Returns
// the decoder (caller destroys) or nullptr with *st set. Defined in
// image_processor.cpp; shared by the synchronous and async entry points.
imgf_decoder_t *open_file_decoder(FILE *fp, long offset, size_t length, const Options &opts,
                                  imgf_file_source_t *fs, Status *st);

// Per-task config for the dual-core async decode (see DecodeJob / decode_file_async).
struct ParallelCfg {
    int producer_core;
    int producer_prio;
    int consumer_core;
    int consumer_prio;
};

// Background two-task decode over imgf_async: the decode (producer) and the
// color+resize+dither+pack (consumer) run on two tasks. Setup (file/decoder open
// + output allocation) runs synchronously on the calling task, then the tasks are
// spawned and this returns immediately. Poll async_decode_done(), reclaim with
// async_decode_join().
struct AsyncDecode;

// *out_status semantics on a nullptr return: a terminal Status (open/decode/OOM)
// is a hard failure to report; Status::Ok means the tasks could not be spawned
// and the caller should fall back to a synchronous decode.
AsyncDecode *async_decode_start(const char *path, const Options &opts, Progress *prog,
                                const ParallelCfg &par, uint32_t offset, uint32_t length,
                                Status *out_status);
bool   async_decode_done(const AsyncDecode *a);
Status async_decode_join(AsyncDecode *a, Image &out);

}  // namespace imgproc
