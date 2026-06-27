/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * In-tree SHA-256 (dependency-free); the WiFi credential KDF uses it.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
} dokan_sha256_ctx;

void dokan_sha256_init(dokan_sha256_ctx *ctx);
void dokan_sha256_update(dokan_sha256_ctx *ctx, const uint8_t *data, size_t len);
void dokan_sha256_final(dokan_sha256_ctx *ctx, uint8_t out[32]);
void dokan_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
