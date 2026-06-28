/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

/* End-to-end runtime test: two sessions opened from one descriptor, rendezvousing
 * over the sim (localhost) transport. Exercises dokan_open, the lock, the I/O
 * task, and teardown. */

#include "dokan.h"
#include "dokan_descriptor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); failures++; } \
} while (0)

typedef struct {
    pthread_mutex_t m;
    bool connected;
    bool opened;
    bool finished;
    int errors;
    uint8_t *buf;
    size_t len, cap;
} Peer;

static void on_event(const dokan_event_t *ev, void *arg) {
    Peer *p = arg;
    pthread_mutex_lock(&p->m);
    switch (ev->type) {
    case DOKAN_EVENT_CONNECTED:      p->connected = true; break;
    case DOKAN_EVENT_STREAM_OPENED:  p->opened = true; break;
    case DOKAN_EVENT_STREAM_DATA:
        if (p->len + ev->len > p->cap) {
            size_t c = p->cap ? p->cap : 4096;
            while (c < p->len + ev->len) c *= 2;
            p->buf = realloc(p->buf, c);
            p->cap = c;
        }
        memcpy(p->buf + p->len, ev->data, ev->len);
        p->len += ev->len;
        break;
    case DOKAN_EVENT_STREAM_FINISHED: p->finished = true; break;
    case DOKAN_EVENT_ERROR:           p->errors++; break;
    default: break;
    }
    pthread_mutex_unlock(&p->m);
}

static bool peer_flag(Peer *p, int which) {
    pthread_mutex_lock(&p->m);
    bool v = which == 0 ? p->connected : p->finished;
    pthread_mutex_unlock(&p->m);
    return v;
}

/* Poll a flag up to timeout_ms. */
static bool wait_flag(Peer *p, int which, int timeout_ms) {
    for (int i = 0; i < timeout_ms; i++) {
        if (peer_flag(p, which)) return true;
        usleep(1000);
    }
    return peer_flag(p, which);
}

int main(void) {
    printf("runtime end-to-end (sim transport):\n");

    const char app[4] = {'N', 'C', 'K', 'N'};
    uint8_t seed[16];
    for (int i = 0; i < 16; i++) seed[i] = (uint8_t)(i * 17 + 3);
    char desc[DOKAN_DESCRIPTOR_MAX];
    CHECK(dokan_descriptor_encode(DOKAN_TRANSPORT_SIM, app, seed, sizeof seed, desc, sizeof desc) == ESP_OK,
          "build SIM descriptor");

    Peer host = {0}, cli = {0};
    pthread_mutex_init(&host.m, NULL);
    pthread_mutex_init(&cli.m, NULL);

    dokan_config_t cfg = {0};
    cfg.connect_timeout_ms = 3000;
    dokan_session_t *hs, *cs;
    CHECK(dokan_open(desc, DOKAN_ROLE_HOST, app, &cfg, on_event, &host, &hs) == ESP_OK, "open host");
    CHECK(dokan_open(desc, DOKAN_ROLE_CLIENT, app, &cfg, on_event, &cli, &cs) == ESP_OK, "open client");

    CHECK(wait_flag(&host, 0, 3000) && wait_flag(&cli, 0, 3000), "both sides connected");

    const size_t N = 100000;
    uint8_t *src = malloc(N);
    for (size_t i = 0; i < N; i++) src[i] = (uint8_t)(i * 29 + 11);

    dokan_stream_opts_t opts = { 1, N };
    dokan_stream_t *st;
    CHECK(dokan_stream_open(hs, &opts, &st) == ESP_OK, "host opened stream");

    size_t sent = 0;
    for (int guard = 0; guard < 200000 && sent < N; guard++) {
        size_t w = 0;
        dokan_stream_write(st, src + sent, N - sent, &w);
        sent += w;
        if (w == 0) usleep(1000);
    }
    dokan_stream_finish(st);

    bool done = wait_flag(&cli, 1, 5000);
    CHECK(sent == N, "all bytes written");
    CHECK(done && cli.opened, "client received stream + FINISHED");
    CHECK(cli.len == N && memcmp(cli.buf, src, N) == 0, "100KB transferred intact over sockets");
    CHECK(host.errors == 0 && cli.errors == 0, "no errors during transfer");

    dokan_close(hs);
    dokan_close(cs);

    free(src);
    free(host.buf);
    free(cli.buf);
    printf("\n%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
