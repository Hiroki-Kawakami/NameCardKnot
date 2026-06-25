/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "image_processor.hpp"

namespace imgproc {

// Bytes per output row for `fmt` at `width` pixels.
size_t out_stride(OutFormat fmt, int width);

// Pack one row of quantized levels (0..n-1) into the destination row.
//   L8: gray byte = level * 255 / (n-1) (so n==16 puts level in the high nibble).
//   I4: 4 bpp, first pixel in the high nibble.
//   I1: 1 bpp, first pixel in the MSB.
void pack_row(OutFormat fmt, const uint8_t *levels, int width, int n, uint8_t *dst);

}  // namespace imgproc
