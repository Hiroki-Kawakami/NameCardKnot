/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstddef>
#include <cstdint>

// Caps-aware allocation for output buffers. On device `caps` are ESP-IDF heap
// capabilities (0 = PSRAM default); on the host they are ignored (malloc).
// img_free MUST be paired with img_alloc — on device the two route to the
// matching heap_caps allocator/free.
namespace imgproc {

void *img_alloc(size_t size, uint32_t caps);
// Internal RAM (device) / malloc (host). Returns null if it won't fit. Used for
// the JPEG band so the decoder stays off the PSRAM bus the EPD/UI core uses.
void *img_alloc_internal(size_t size);
void  img_free(void *p);

}  // namespace imgproc
