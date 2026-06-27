/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

/* Host unit test: the data-plane engine. Two sessions wired back-to-back over an
 * in-memory pipe, pumped deterministically. No ESP-IDF, no threads. */

#include "dokan.h"
#include "dokan_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); failures++; } \
} while (0)

#define OUTCAP (1u << 18)  /* 256 KB; window-bounded between pumps */

typedef struct {
    uint8_t *buf;
    size_t   len, cap;
    bool     finished, reset, paused_once;
    uint16_t kind;
    uint64_t size_hint;
    dokan_stream_t *st;
} RecvRec;

typedef struct {
    dokan_session_t *s;
    uint8_t *out;
    size_t   out_len;
    size_t   write_cap;        /* max bytes accepted per transport write */
    /* behavior flags for the peer side */
    bool     auto_reset_on_open;
    esp_err_t reset_reason;
    bool     auto_pause;
    /* observations */
    RecvRec  recv[16];
    int      recv_count;
    int      reset_events;
    esp_err_t last_reset_err;
    int      writable_events;
    int      disconnect_events;
    int      error_events;
} Endpoint;

static size_t ep_write(void *ctx, const uint8_t *data, size_t len) {
    Endpoint *e = ctx;
    size_t take = len;
    if (take > e->write_cap) take = e->write_cap;
    if (e->out_len + take > OUTCAP) take = OUTCAP - e->out_len;
    memcpy(e->out + e->out_len, data, take);
    e->out_len += take;
    return take;
}

static void rr_append(RecvRec *r, const uint8_t *data, size_t len) {
    if (r->len + len > r->cap) {
        size_t c = r->cap ? r->cap : 1024;
        while (c < r->len + len) c *= 2;
        r->buf = realloc(r->buf, c);
        r->cap = c;
    }
    memcpy(r->buf + r->len, data, len);
    r->len += len;
}

static void on_event(const dokan_event_t *ev, void *arg) {
    Endpoint *e = arg;
    switch (ev->type) {
    case DOKAN_EVENT_STREAM_OPENED: {
        RecvRec *r = &e->recv[e->recv_count++];
        r->st = ev->stream;
        r->kind = ev->opts.kind;
        r->size_hint = ev->opts.size_hint;
        dokan_stream_set_user(ev->stream, r);
        if (e->auto_reset_on_open) dokan_stream_reset(ev->stream, e->reset_reason);
        break;
    }
    case DOKAN_EVENT_STREAM_DATA: {
        RecvRec *r = dokan_stream_get_user(ev->stream);
        if (r) {
            rr_append(r, ev->data, ev->len);
            if (e->auto_pause && !r->paused_once) { dokan_stream_pause(ev->stream); r->paused_once = true; }
        }
        break;
    }
    case DOKAN_EVENT_STREAM_FINISHED: {
        RecvRec *r = dokan_stream_get_user(ev->stream);
        if (r) r->finished = true;
        break;
    }
    case DOKAN_EVENT_STREAM_RESET: {
        RecvRec *r = dokan_stream_get_user(ev->stream);
        if (r) r->reset = true;
        e->reset_events++;
        e->last_reset_err = ev->err;
        break;
    }
    case DOKAN_EVENT_STREAM_WRITABLE: e->writable_events++; break;
    case DOKAN_EVENT_DISCONNECTED:    e->disconnect_events++; break;
    case DOKAN_EVENT_ERROR:           e->error_events++; break;
    default: break;
    }
}

static Endpoint *ep_new(dokan_role_t role, const dokan_config_t *cfg) {
    Endpoint *e = calloc(1, sizeof *e);
    e->out = malloc(OUTCAP);
    e->write_cap = OUTCAP;
    dokan_session_create_raw(role, cfg, ep_write, e, on_event, e, &e->s);
    return e;
}

static void ep_free(Endpoint *e) {
    dokan_close(e->s);
    for (int i = 0; i < e->recv_count; i++) free(e->recv[i].buf);
    free(e->out);
    free(e);
}

static void pump(Endpoint *a, Endpoint *b) {
    for (int i = 0; i < 1000000; i++) {
        dokan_session_on_writable(a->s);
        dokan_session_on_writable(b->s);
        bool moved = false;
        if (a->out_len) { size_t n = a->out_len; a->out_len = 0; dokan_session_feed(b->s, a->out, n); moved = true; }
        if (b->out_len) { size_t n = b->out_len; b->out_len = 0; dokan_session_feed(a->s, b->out, n); moved = true; }
        if (!moved) break;
    }
}

/* ---- tests --------------------------------------------------------------- */

static void test_basic(void) {
    printf("basic small transfer:\n");
    Endpoint *A = ep_new(DOKAN_ROLE_HOST, NULL), *B = ep_new(DOKAN_ROLE_CLIENT, NULL);

    dokan_stream_opts_t opts = { 7, 5 };
    dokan_stream_t *st;
    dokan_stream_open(A->s, &opts, &st);
    size_t w;
    dokan_stream_write(st, "hello", 5, &w);
    dokan_stream_finish(st);
    pump(A, B);

    CHECK(B->recv_count == 1, "peer saw one stream");
    CHECK(B->recv[0].kind == 7 && B->recv[0].size_hint == 5, "opts (kind/size_hint) carried");
    CHECK(w == 5 && B->recv[0].len == 5 && memcmp(B->recv[0].buf, "hello", 5) == 0, "payload matches");
    CHECK(B->recv[0].finished, "FINISHED delivered");
    ep_free(A); ep_free(B);
}

static void test_large(void) {
    printf("large transfer (window cycles):\n");
    Endpoint *A = ep_new(DOKAN_ROLE_HOST, NULL), *B = ep_new(DOKAN_ROLE_CLIENT, NULL);

    const size_t N = 200000;
    uint8_t *src = malloc(N);
    for (size_t i = 0; i < N; i++) src[i] = (uint8_t)(i * 31 + 7);

    dokan_stream_opts_t opts = { 1, N };
    dokan_stream_t *st;
    dokan_stream_open(A->s, &opts, &st);

    size_t sent = 0;
    bool finished = false;
    for (int guard = 0; guard < 100000; guard++) {
        if (sent < N) {
            size_t w;
            dokan_stream_write(st, src + sent, N - sent, &w);
            sent += w;
        }
        if (sent == N && !finished) { dokan_stream_finish(st); finished = true; }
        pump(A, B);
        if (finished && B->recv_count == 1 && B->recv[0].finished) break;
    }

    CHECK(sent == N, "all bytes accepted by write()");
    CHECK(B->recv_count == 1 && B->recv[0].len == N, "receiver got every byte");
    CHECK(B->recv[0].buf && memcmp(B->recv[0].buf, src, N) == 0, "content intact");
    CHECK(B->recv[0].finished, "FINISHED after large transfer");
    CHECK(A->writable_events > 0, "window backpressure exercised (WRITABLE fired)");
    free(src);
    ep_free(A); ep_free(B);
}

static void test_multi(void) {
    printf("two concurrent streams:\n");
    Endpoint *A = ep_new(DOKAN_ROLE_HOST, NULL), *B = ep_new(DOKAN_ROLE_CLIENT, NULL);

    dokan_stream_t *s1, *s2;
    dokan_stream_opts_t o1 = { 10, 3 }, o2 = { 20, 3 };
    dokan_stream_open(A->s, &o1, &s1);
    dokan_stream_open(A->s, &o2, &s2);
    size_t w;
    dokan_stream_write(s1, "aaa", 3, &w);
    dokan_stream_write(s2, "bbb", 3, &w);
    dokan_stream_write(s1, "AAA", 3, &w);
    dokan_stream_finish(s1);
    dokan_stream_finish(s2);
    pump(A, B);

    CHECK(B->recv_count == 2, "both streams opened");
    /* match by kind regardless of arrival order */
    RecvRec *r10 = NULL, *r20 = NULL;
    for (int i = 0; i < B->recv_count; i++) {
        if (B->recv[i].kind == 10) r10 = &B->recv[i];
        if (B->recv[i].kind == 20) r20 = &B->recv[i];
    }
    CHECK(r10 && r10->len == 6 && memcmp(r10->buf, "aaaAAA", 6) == 0, "stream 1 reassembled in order");
    CHECK(r20 && r20->len == 3 && memcmp(r20->buf, "bbb", 3) == 0, "stream 2 independent");
    CHECK(r10 && r10->finished && r20 && r20->finished, "both finished");
    ep_free(A); ep_free(B);
}

static void test_pause(void) {
    printf("pause/resume backpressure:\n");
    Endpoint *A = ep_new(DOKAN_ROLE_HOST, NULL), *B = ep_new(DOKAN_ROLE_CLIENT, NULL);
    B->auto_pause = true;

    const size_t N = 120000;  /* > window, so a paused reader stalls the sender */
    uint8_t *src = malloc(N);
    for (size_t i = 0; i < N; i++) src[i] = (uint8_t)(i * 7 + 1);

    dokan_stream_opts_t opts = { 1, N };
    dokan_stream_t *st;
    dokan_stream_open(A->s, &opts, &st);

    /* push until the window is exhausted by the paused reader */
    size_t sent = 0;
    for (int i = 0; i < 100; i++) {
        size_t w;
        dokan_stream_write(st, src + sent, N - sent, &w);
        sent += w;
        pump(A, B);
        if (w == 0) break;
    }
    CHECK(sent < N, "paused reader stalls the sender (sent < N)");
    CHECK(B->recv_count == 1 && B->recv[0].len < N, "reader received only up to the window");

    /* resume and drain the rest */
    dokan_stream_resume(B->recv[0].st);
    bool finished = false;
    for (int guard = 0; guard < 100000; guard++) {
        if (sent < N) { size_t w; dokan_stream_write(st, src + sent, N - sent, &w); sent += w; }
        if (sent == N && !finished) { dokan_stream_finish(st); finished = true; }
        pump(A, B);
        if (finished && B->recv[0].finished) break;
    }
    CHECK(sent == N && B->recv[0].len == N, "transfer completes after resume");
    CHECK(memcmp(B->recv[0].buf, src, N) == 0, "content intact across pause/resume");
    free(src);
    ep_free(A); ep_free(B);
}

static void test_reset(void) {
    printf("receiver reset propagates to sender:\n");
    Endpoint *A = ep_new(DOKAN_ROLE_HOST, NULL), *B = ep_new(DOKAN_ROLE_CLIENT, NULL);
    B->auto_reset_on_open = true;
    B->reset_reason = ESP_ERR_TIMEOUT;

    dokan_stream_t *st;
    dokan_stream_open(A->s, NULL, &st);
    size_t w;
    dokan_stream_write(st, "data", 4, &w);
    pump(A, B);

    CHECK(A->reset_events == 1, "sender got STREAM_RESET");
    CHECK(A->last_reset_err == ESP_ERR_TIMEOUT, "reset reason carried");
    CHECK(A->error_events == 0 && B->error_events == 0, "no protocol error (in-flight DATA dropped)");
    ep_free(A); ep_free(B);
}

static void test_bidirectional(void) {
    printf("bidirectional streams:\n");
    Endpoint *A = ep_new(DOKAN_ROLE_HOST, NULL), *B = ep_new(DOKAN_ROLE_CLIENT, NULL);

    dokan_stream_t *a2b, *b2a;
    dokan_stream_opts_t oa = { 1, 4 }, ob = { 2, 3 };
    dokan_stream_open(A->s, &oa, &a2b);
    dokan_stream_open(B->s, &ob, &b2a);
    size_t w;
    dokan_stream_write(a2b, "ping", 4, &w);
    dokan_stream_write(b2a, "pong", 4, &w);
    dokan_stream_finish(a2b);
    dokan_stream_finish(b2a);
    pump(A, B);

    CHECK(B->recv_count == 1 && B->recv[0].len == 4 && memcmp(B->recv[0].buf, "ping", 4) == 0,
          "A->B stream received");
    CHECK(A->recv_count == 1 && A->recv[0].len == 4 && memcmp(A->recv[0].buf, "pong", 4) == 0,
          "B->A stream received");
    ep_free(A); ep_free(B);
}

static void test_transport_backpressure(void) {
    printf("transport partial writes (outbound queue):\n");
    Endpoint *A = ep_new(DOKAN_ROLE_HOST, NULL), *B = ep_new(DOKAN_ROLE_CLIENT, NULL);
    A->write_cap = 100;  /* transport accepts only 100 bytes per write */
    B->write_cap = 100;

    const size_t N = 50000;
    uint8_t *src = malloc(N);
    for (size_t i = 0; i < N; i++) src[i] = (uint8_t)(i * 13 + 5);

    dokan_stream_opts_t opts = { 1, N };
    dokan_stream_t *st;
    dokan_stream_open(A->s, &opts, &st);

    size_t sent = 0;
    bool finished = false;
    for (int guard = 0; guard < 1000000; guard++) {
        if (sent < N) { size_t w; dokan_stream_write(st, src + sent, N - sent, &w); sent += w; }
        if (sent == N && !finished) { dokan_stream_finish(st); finished = true; }
        pump(A, B);
        if (finished && B->recv_count == 1 && B->recv[0].finished) break;
    }
    CHECK(sent == N && B->recv_count == 1 && B->recv[0].len == N, "completes despite capped transport");
    CHECK(memcmp(B->recv[0].buf, src, N) == 0, "content intact through outbound queue");
    free(src);
    ep_free(A); ep_free(B);
}

int main(void) {
    test_basic();
    test_large();
    test_multi();
    test_pause();
    test_reset();
    test_bidirectional();
    test_transport_backpressure();
    printf("\n%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
