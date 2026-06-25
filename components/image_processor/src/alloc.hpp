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
void  img_free(void *p);

}  // namespace imgproc
