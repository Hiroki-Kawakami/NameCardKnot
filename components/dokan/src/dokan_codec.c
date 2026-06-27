/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "dokan_codec.h"

static const char B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

size_t dokan_base64url_encode(const uint8_t *in, size_t inlen, char *out, size_t outcap) {
    size_t need = (inlen / 3) * 4;
    size_t rem = inlen % 3;
    if (rem) need += rem + 1;
    if (outcap < need) return 0;

    size_t o = 0, i = 0;
    for (; i + 3 <= inlen; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64URL[(v >> 18) & 0x3f];
        out[o++] = B64URL[(v >> 12) & 0x3f];
        out[o++] = B64URL[(v >> 6) & 0x3f];
        out[o++] = B64URL[v & 0x3f];
    }
    if (rem == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64URL[(v >> 18) & 0x3f];
        out[o++] = B64URL[(v >> 12) & 0x3f];
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64URL[(v >> 18) & 0x3f];
        out[o++] = B64URL[(v >> 12) & 0x3f];
        out[o++] = B64URL[(v >> 6) & 0x3f];
    }
    return o;
}

static int b64url_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

int dokan_base64url_decode(const char *in, size_t inlen,
                           uint8_t *out, size_t outcap, size_t *outlen) {
    if (inlen % 4 == 1) return -1;
    size_t produced = (inlen / 4) * 3;
    size_t rem = inlen % 4;
    if (rem == 2) produced += 1;
    else if (rem == 3) produced += 2;
    if (outcap < produced) return -1;

    size_t o = 0, i = 0;
    for (; i + 4 <= inlen; i += 4) {
        int a = b64url_val(in[i]), b = b64url_val(in[i + 1]);
        int c = b64url_val(in[i + 2]), d = b64url_val(in[i + 3]);
        if ((a | b | c | d) < 0) return -1;
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | d;
        out[o++] = (uint8_t)(v >> 16);
        out[o++] = (uint8_t)(v >> 8);
        out[o++] = (uint8_t)v;
    }
    if (rem == 2) {
        int a = b64url_val(in[i]), b = b64url_val(in[i + 1]);
        if ((a | b) < 0) return -1;
        out[o++] = (uint8_t)((a << 2) | (b >> 4));
    } else if (rem == 3) {
        int a = b64url_val(in[i]), b = b64url_val(in[i + 1]), c = b64url_val(in[i + 2]);
        if ((a | b | c) < 0) return -1;
        out[o++] = (uint8_t)((a << 2) | (b >> 4));
        out[o++] = (uint8_t)((b << 4) | (c >> 2));
    }
    *outlen = o;
    return 0;
}

size_t dokan_base32_encode(const uint8_t *in, size_t inlen, char *out, size_t outcap) {
    size_t need = (inlen * 8 + 4) / 5;
    if (outcap < need) return 0;

    uint32_t buf = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < inlen; i++) {
        buf = (buf << 8) | in[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[o++] = B32[(buf >> bits) & 0x1f];
        }
    }
    if (bits > 0)
        out[o++] = B32[(buf << (5 - bits)) & 0x1f];
    return o;
}

uint8_t dokan_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}
