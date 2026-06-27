/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Small codecs for descriptor serialization: base64url, base32, CRC-8.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

/* base64url, no padding. encode -> chars written (0 if outcap too small);
 * decode -> 0 ok / -1 fail, byte count in *outlen. */
size_t dokan_base64url_encode(const uint8_t *in, size_t inlen, char *out, size_t outcap);
int    dokan_base64url_decode(const char *in, size_t inlen,
                              uint8_t *out, size_t outcap, size_t *outlen);

/* base32 RFC4648 (uppercase, no padding), encode only -> chars written. */
size_t dokan_base32_encode(const uint8_t *in, size_t inlen, char *out, size_t outcap);

/* CRC-8 (poly 0x07, init 0x00). */
uint8_t dokan_crc8(const uint8_t *data, size_t len);
