/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

/* Host unit test: the descriptor codec and KDF (pure parts). No ESP-IDF (gcc). */

#include "dokan.h"
#include "dokan_descriptor.h"
#include "dokan_codec.h"
#include "dokan_sha256.h"
#include "dokan_wifi_params.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); failures++; } \
} while (0)

static void hex(const uint8_t *b, size_t n, char *out) {
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i * 2] = H[b[i] >> 4]; out[i * 2 + 1] = H[b[i] & 0xf]; }
    out[n * 2] = '\0';
}

static void test_sha256(void) {
    printf("sha256:\n");
    uint8_t d[32];
    char h[65];

    dokan_sha256((const uint8_t *)"", 0, d);
    hex(d, 32, h);
    CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
          "SHA256(\"\")");

    dokan_sha256((const uint8_t *)"abc", 3, d);
    hex(d, 32, h);
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
          "SHA256(\"abc\")");

    const char *s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    dokan_sha256((const uint8_t *)s, strlen(s), d);
    hex(d, 32, h);
    CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0,
          "SHA256(56-byte msg, spans 2 blocks)");
}

static void test_codecs(void) {
    printf("codecs:\n");

    uint8_t in[3] = {0xff, 0xff, 0xff};
    char b64[8];
    size_t n = dokan_base64url_encode(in, 3, b64, sizeof b64);
    b64[n] = '\0';
    CHECK(n == 4 && strcmp(b64, "____") == 0, "base64url(0xffffff) == ____");

    uint8_t rt[8];
    size_t rtn = 0;
    int r = dokan_base64url_decode(b64, n, rt, sizeof rt, &rtn);
    CHECK(r == 0 && rtn == 3 && memcmp(rt, in, 3) == 0, "base64url roundtrip");

    CHECK(dokan_base64url_decode("A", 1, rt, sizeof rt, &rtn) == -1, "base64url reject len%%4==1");
    CHECK(dokan_base64url_decode("**", 2, rt, sizeof rt, &rtn) == -1, "base64url reject bad char");

    char b32[16];
    size_t bn = dokan_base32_encode((const uint8_t *)"foobar", 6, b32, sizeof b32);
    b32[bn] = '\0';
    CHECK(strcmp(b32, "MZXW6YTBOI") == 0, "base32(\"foobar\") == MZXW6YTBOI");

    CHECK(dokan_crc8((const uint8_t *)"123456789", 9) == 0xf4, "crc8 check value 0xF4");
}

static void test_kdf(void) {
    printf("kdf (derive):\n");
    char a_ssid[DOKAN_WIFI_SSID_MAX], a_psk[DOKAN_WIFI_PSK_MAX];
    char b_ssid[DOKAN_WIFI_SSID_MAX], b_psk[DOKAN_WIFI_PSK_MAX];
    uint8_t seed1[16], seed2[16];
    for (int i = 0; i < 16; i++) { seed1[i] = (uint8_t)i; seed2[i] = (uint8_t)(i + 1); }

    dokan_wifi_derive("NCKN", seed1, a_ssid, a_psk);
    dokan_wifi_derive("NCKN", seed1, b_ssid, b_psk);
    CHECK(strcmp(a_ssid, b_ssid) == 0 && strcmp(a_psk, b_psk) == 0,
          "same app_id+seed -> identical SSID/PSK (deterministic)");

    dokan_wifi_derive("NCKN", seed2, b_ssid, b_psk);
    CHECK(strcmp(a_ssid, b_ssid) != 0, "different seed -> different SSID");

    char other[DOKAN_WIFI_SSID_MAX];
    dokan_wifi_derive("XXXX", seed1, other, b_psk);
    CHECK(strcmp(a_ssid, other) != 0, "different app_id (salt) -> different SSID");

    CHECK(strncmp(a_ssid, "dokan-", 6) == 0 && strlen(a_ssid) == 13, "SSID form dokan-XXXXXXX");
    CHECK(strlen(a_psk) >= 8 && strlen(a_psk) <= 63, "PSK length 8..63 (WPA2)");
}

static void test_descriptor(void) {
    printf("descriptor:\n");
    const char app[4] = {'N', 'C', 'K', 'N'};

    char d[DOKAN_DESCRIPTOR_MAX];
    esp_err_t err = dokan_descriptor_create(DOKAN_TRANSPORT_WIFI, app, d, sizeof d);
    CHECK(err == ESP_OK, "create() ESP_OK");
    CHECK(strlen(d) <= DOKAN_DESCRIPTOR_MAX, "len <= 128");
    CHECK(strncmp(d, "DK1NCKNW", 8) == 0, "header DK1NCKNW");
    CHECK(dokan_descriptor_valid(d, app), "valid() true");

    const char other[4] = {'X', 'X', 'X', 'X'};
    CHECK(!dokan_descriptor_valid(d, other), "different app_id -> invalid");

    char broken[DOKAN_DESCRIPTOR_MAX];
    strcpy(broken, d);
    size_t L = strlen(broken);
    broken[L - 1] = (broken[L - 1] == 'A') ? 'B' : 'A';
    CHECK(!dokan_descriptor_valid(broken, app), "corrupted crc -> invalid");

    /* fixed params -> golden vector, parsed back */
    dokan_wifi_params_t p = {0};
    p.channel = 6;
    p.port = 3939;
    for (int i = 0; i < 16; i++) p.seed[i] = (uint8_t)(0x10 + i);
    uint8_t pbin[DOKAN_WIFI_PARAMS_BIN_LEN];
    dokan_wifi_params_encode(&p, pbin);
    char g[DOKAN_DESCRIPTOR_MAX];
    CHECK(dokan_descriptor_encode(DOKAN_TRANSPORT_WIFI, app, pbin, sizeof pbin, g, sizeof g) == ESP_OK,
          "encode(fixed params) ESP_OK");

    dokan_transport_id_t t;
    uint8_t pbuf[DOKAN_PARAMS_MAX];
    size_t plen = 0;
    CHECK(dokan_descriptor_parse(g, app, &t, pbuf, sizeof pbuf, &plen) == ESP_OK, "parse() ESP_OK");
    dokan_wifi_params_t q;
    CHECK(t == DOKAN_TRANSPORT_WIFI && plen == DOKAN_WIFI_PARAMS_BIN_LEN &&
          dokan_wifi_params_decode(pbuf, plen, &q) == ESP_OK &&
          q.channel == 6 && q.port == 3939 && memcmp(q.seed, p.seed, 16) == 0,
          "parse roundtrip == original params");

    char g2[DOKAN_DESCRIPTOR_MAX];
    dokan_descriptor_encode(DOKAN_TRANSPORT_WIFI, app, pbin, sizeof pbin, g2, sizeof g2);
    CHECK(strcmp(g, g2) == 0, "encode is deterministic");
    printf("    golden = %s (len %zu)\n", g, strlen(g));

    char small[10];
    CHECK(dokan_descriptor_create(DOKAN_TRANSPORT_WIFI, app, small, sizeof small) == ESP_ERR_INVALID_SIZE,
          "cap too small -> ESP_ERR_INVALID_SIZE");

    CHECK(dokan_descriptor_create(DOKAN_TRANSPORT_BT, app, d, sizeof d) == ESP_ERR_NOT_SUPPORTED,
          "unsupported transport -> ESP_ERR_NOT_SUPPORTED");
}

int main(void) {
    test_sha256();
    test_codecs();
    test_kdf();
    test_descriptor();
    printf("\n%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
