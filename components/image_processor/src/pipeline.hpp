/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include "image_processor.hpp"
#include "rowsource.hpp"

namespace imgproc {

// Streams `src` top-to-bottom through downscale -> color -> dither -> pack and
// fills `out` (allocated here). Holds only per-row working buffers plus the
// output, never the full-resolution source. Decoder-agnostic: also driven
// directly by the host tests with a synthetic RowSource.
Status run_pipeline(RowSource &src, const Options &opts, Image &out, Progress *prog = nullptr);

}  // namespace imgproc
