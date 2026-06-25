/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "image_processor.hpp"

namespace imgproc {

enum class Format { Unknown, Jpeg, Png };

// Identify the container from its leading bytes (needs >= 8 for PNG, >= 3 for JPEG).
Format sniff(const uint8_t *hdr, size_t n);

// Pre-allocation guard: rejects zero/oversized source dimensions before any
// full-resolution work. max_pixels == 0 disables the size cap.
Status check_src_size(uint32_t w, uint32_t h, uint32_t max_pixels);

}  // namespace imgproc
