/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "alloc.hpp"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#else
#include <cstdlib>
#endif

namespace imgproc {

#ifdef ESP_PLATFORM
static constexpr uint32_t kDefaultCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif

void *img_alloc(size_t size, uint32_t caps) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, caps ? caps : kDefaultCaps);
#else
    (void)caps;
    return std::malloc(size);
#endif
}

void *img_alloc_internal(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return std::malloc(size);
#endif
}

void img_free(void *p) {
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    std::free(p);
#endif
}

}  // namespace imgproc
