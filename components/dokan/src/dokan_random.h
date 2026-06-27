/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Cryptographic random byte fill (device esp_fill_random / host /dev/urandom).
 */

#pragma once
#include <stddef.h>
#include "esp_err.h"

esp_err_t dokan_random_fill(void *buf, size_t len);
