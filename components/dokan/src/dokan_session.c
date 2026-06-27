/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Data-plane engine implementation: frame codec, stream table, per-stream
 * window flow control, pause/resume, GOAWAY. See dokan_session.h.
 */

#include "dokan.h"
#include "dokan_session.h"
#include <stdlib.h>
#include <string.h>

#define DOKAN_DEFAULT_WINDOW    32768u
#define DOKAN_MAX_CHUNK         4096u
#define DOKAN_MAX_FRAME_PAYLOAD 4096u
#define DOKAN_MAX_STREAMS       16

enum {
    FRAME_OPEN   = 0x01,  /* payload: kind:u16 size_hint:u64 */
    FRAME_DATA   = 0x02,
    FRAME_EOF    = 0x03,
    FRAME_RESET  = 0x04,  /* payload: reason:i32 */
    FRAME_WINDOW = 0x05,  /* payload: credit:u32 */
    FRAME_GOAWAY = 0x06,  /* payload: reason:i32 */
};

struct dokan_stream {
    struct dokan_stream *next;
    dokan_session_t *session;
    uint16_t id;
    bool     local;          /* opened here (we write) vs by peer (we read) */
    bool     reset;
    void    *user;

    uint32_t send_window;    /* local: bytes we may still send */
    bool     finished_sent;

    bool     paused;         /* peer: reader paused */
    bool     finished_recv;  /* peer: EOF seen */
    uint32_t recv_outstanding;  /* received but not yet credited; <= window */
    uint32_t credit_pending;    /* consumed bytes awaiting a WINDOW flush */
    uint8_t *rx_buf;            /* data held while paused */
    size_t   rx_cap, rx_len;
};

struct dokan_session {
    dokan_role_t role;
    uint32_t window;
    dokan_transport_write_fn write_fn;
    void *write_ctx;
    dokan_event_cb_t event_cb;
    void *event_arg;

    uint16_t next_tx_id;
    int      stream_count;
    dokan_stream_t *streams;

    bool closed, error, goaway_received;

    uint8_t in_buf[5 + DOKAN_MAX_FRAME_PAYLOAD];
    size_t  in_len;

    uint8_t *out_buf;
    size_t   out_cap, out_head, out_tail;
};

/* ---- byte helpers (big-endian) ------------------------------------------- */

static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static void wr64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> ((7 - i) * 8));
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

/* ---- outbound queue ------------------------------------------------------ */

static void out_flush(dokan_session_t *s) {
    while (s->out_head < s->out_tail) {
        size_t w = s->write_fn(s->write_ctx, s->out_buf + s->out_head, s->out_tail - s->out_head);
        if (w == 0) break;
        s->out_head += w;
    }
    if (s->out_head == s->out_tail) s->out_head = s->out_tail = 0;
}

static void enqueue(dokan_session_t *s, uint8_t type, uint16_t sid,
                    const uint8_t *payload, uint16_t plen) {
    size_t need = (size_t)5 + plen;
    if (s->out_tail + need > s->out_cap) {
        if (s->out_head > 0) {
            memmove(s->out_buf, s->out_buf + s->out_head, s->out_tail - s->out_head);
            s->out_tail -= s->out_head;
            s->out_head = 0;
        }
        if (s->out_tail + need > s->out_cap) {
            size_t cap = s->out_cap ? s->out_cap : 256;
            while (cap < s->out_tail + need) cap *= 2;
            uint8_t *nb = realloc(s->out_buf, cap);
            if (!nb) { s->error = true; return; }
            s->out_buf = nb;
            s->out_cap = cap;
        }
    }
    uint8_t *p = s->out_buf + s->out_tail;
    p[0] = type;
    wr16(p + 1, sid);
    wr16(p + 3, plen);
    if (plen) memcpy(p + 5, payload, plen);
    s->out_tail += need;
    out_flush(s);
}

/* ---- events -------------------------------------------------------------- */

static void emit(dokan_session_t *s, dokan_event_type_t type, dokan_stream_t *st,
                 const uint8_t *data, size_t len, const dokan_stream_opts_t *opts, esp_err_t err) {
    if (!s->event_cb) return;
    dokan_event_t ev;
    ev.type = type;
    ev.session = s;
    ev.stream = st;
    ev.data = data;
    ev.len = len;
    ev.opts = opts ? *opts : (dokan_stream_opts_t){0, 0};
    ev.err = err;
    s->event_cb(&ev, s->event_arg);
}

static void protocol_error(dokan_session_t *s, esp_err_t err) {
    if (s->error) return;
    s->error = true;
    emit(s, DOKAN_EVENT_ERROR, NULL, NULL, 0, NULL, err);
}

/* ---- stream table -------------------------------------------------------- */

static dokan_stream_t *find_stream(dokan_session_t *s, uint16_t id) {
    for (dokan_stream_t *st = s->streams; st; st = st->next)
        if (st->id == id) return st;
    return NULL;
}

static dokan_stream_t *new_stream(dokan_session_t *s, uint16_t id, bool local) {
    dokan_stream_t *st = calloc(1, sizeof *st);
    if (!st) return NULL;
    st->session = s;
    st->id = id;
    st->local = local;
    st->next = s->streams;
    s->streams = st;
    s->stream_count++;
    return st;
}

static void free_stream(dokan_session_t *s, dokan_stream_t *st) {
    dokan_stream_t **pp = &s->streams;
    while (*pp && *pp != st) pp = &(*pp)->next;
    if (*pp) { *pp = st->next; s->stream_count--; }
    free(st->rx_buf);
    free(st);
}

/* ---- receive flow control ------------------------------------------------ */

static void flush_credit(dokan_session_t *s, dokan_stream_t *st) {
    if (st->credit_pending == 0) return;
    uint8_t p[4];
    wr32(p, st->credit_pending);
    enqueue(s, FRAME_WINDOW, st->id, p, 4);
    st->recv_outstanding -= st->credit_pending;
    st->credit_pending = 0;
}

static void credit_consume(dokan_session_t *s, dokan_stream_t *st, uint32_t n) {
    st->credit_pending += n;
    uint32_t threshold = s->window / 2;
    if (threshold == 0) threshold = 1;
    if (st->credit_pending >= threshold) flush_credit(s, st);
}

static void deliver_finished(dokan_session_t *s, dokan_stream_t *st) {
    flush_credit(s, st);
    emit(s, DOKAN_EVENT_STREAM_FINISHED, st, NULL, 0, NULL, ESP_OK);
}

/* ---- inbound frame handlers --------------------------------------------- */

static void handle_open(dokan_session_t *s, uint16_t sid, const uint8_t *payload, uint16_t plen) {
    unsigned want = (s->role == DOKAN_ROLE_HOST) ? 1u : 0u;  /* peer's id parity */
    if (sid == 0 || (sid & 1u) != want) { protocol_error(s, ESP_ERR_INVALID_ARG); return; }
    if (plen != 10) { protocol_error(s, ESP_ERR_INVALID_SIZE); return; }
    if (find_stream(s, sid)) { protocol_error(s, ESP_ERR_INVALID_STATE); return; }
    if (s->stream_count >= DOKAN_MAX_STREAMS) {
        uint8_t r[4];
        wr32(r, (uint32_t)ESP_ERR_NO_MEM);
        enqueue(s, FRAME_RESET, sid, r, 4);
        return;
    }
    dokan_stream_t *st = new_stream(s, sid, false);
    if (!st) { protocol_error(s, ESP_ERR_NO_MEM); return; }
    dokan_stream_opts_t opts = { rd16(payload), rd64(payload + 2) };
    emit(s, DOKAN_EVENT_STREAM_OPENED, st, NULL, 0, &opts, ESP_OK);
}

static void handle_data(dokan_session_t *s, uint16_t sid, const uint8_t *payload, uint16_t plen) {
    dokan_stream_t *st = find_stream(s, sid);
    if (!st || st->reset) return;  /* unknown / in-flight after reset: drop */
    if (st->local || st->finished_recv) { protocol_error(s, ESP_ERR_INVALID_STATE); return; }
    if ((uint64_t)st->recv_outstanding + plen > s->window) {
        protocol_error(s, ESP_ERR_INVALID_SIZE);  /* peer exceeded its window */
        return;
    }
    st->recv_outstanding += plen;

    if (st->paused) {
        if (st->rx_len + plen > st->rx_cap) {
            size_t cap = st->rx_cap ? st->rx_cap : 1024;
            while (cap < st->rx_len + plen) cap *= 2;
            uint8_t *nb = realloc(st->rx_buf, cap);
            if (!nb) { protocol_error(s, ESP_ERR_NO_MEM); return; }
            st->rx_buf = nb;
            st->rx_cap = cap;
        }
        memcpy(st->rx_buf + st->rx_len, payload, plen);
        st->rx_len += plen;
        return;
    }

    emit(s, DOKAN_EVENT_STREAM_DATA, st, payload, plen, NULL, ESP_OK);
    st = find_stream(s, sid);                 /* callback may have released it */
    if (!st || st->reset) return;
    credit_consume(s, st, plen);
}

static void handle_eof(dokan_session_t *s, uint16_t sid, uint16_t plen) {
    if (plen != 0) { protocol_error(s, ESP_ERR_INVALID_SIZE); return; }
    dokan_stream_t *st = find_stream(s, sid);
    if (!st || st->reset) return;
    if (st->local || st->finished_recv) { protocol_error(s, ESP_ERR_INVALID_STATE); return; }
    st->finished_recv = true;
    if (!st->paused && st->rx_len == 0) deliver_finished(s, st);
}

static void handle_reset(dokan_session_t *s, uint16_t sid, const uint8_t *payload, uint16_t plen) {
    if (plen != 4) { protocol_error(s, ESP_ERR_INVALID_SIZE); return; }
    dokan_stream_t *st = find_stream(s, sid);
    if (!st || st->reset) return;
    st->reset = true;
    emit(s, DOKAN_EVENT_STREAM_RESET, st, NULL, 0, NULL, (esp_err_t)(int32_t)rd32(payload));
}

static void handle_window(dokan_session_t *s, uint16_t sid, const uint8_t *payload, uint16_t plen) {
    if (plen != 4) { protocol_error(s, ESP_ERR_INVALID_SIZE); return; }
    dokan_stream_t *st = find_stream(s, sid);
    if (!st || st->reset) return;
    if (!st->local) { protocol_error(s, ESP_ERR_INVALID_STATE); return; }
    uint32_t old = st->send_window;
    st->send_window += rd32(payload);
    if (!st->finished_sent && old == 0 && st->send_window > 0)
        emit(s, DOKAN_EVENT_STREAM_WRITABLE, st, NULL, 0, NULL, ESP_OK);
}

static void handle_goaway(dokan_session_t *s) {
    s->goaway_received = true;
    emit(s, DOKAN_EVENT_DISCONNECTED, NULL, NULL, 0, NULL, ESP_OK);
}

static void process_frame(dokan_session_t *s, uint8_t type, uint16_t sid,
                          const uint8_t *payload, uint16_t plen) {
    switch (type) {
    case FRAME_OPEN:   handle_open(s, sid, payload, plen);   break;
    case FRAME_DATA:   handle_data(s, sid, payload, plen);   break;
    case FRAME_EOF:    handle_eof(s, sid, plen);             break;
    case FRAME_RESET:  handle_reset(s, sid, payload, plen);  break;
    case FRAME_WINDOW: handle_window(s, sid, payload, plen); break;
    case FRAME_GOAWAY: handle_goaway(s);                     break;
    default:           protocol_error(s, ESP_ERR_NOT_SUPPORTED);
    }
}

/* ---- public / internal API ---------------------------------------------- */

esp_err_t dokan_session_create_raw(dokan_role_t role, const dokan_config_t *cfg,
                                   dokan_transport_write_fn write_fn, void *write_ctx,
                                   dokan_event_cb_t event_cb, void *event_arg,
                                   dokan_session_t **out) {
    if (!write_fn || !out) return ESP_ERR_INVALID_ARG;
    dokan_session_t *s = calloc(1, sizeof *s);
    if (!s) return ESP_ERR_NO_MEM;
    s->role = role;
    s->window = (cfg && cfg->stream_window) ? cfg->stream_window : DOKAN_DEFAULT_WINDOW;
    s->write_fn = write_fn;
    s->write_ctx = write_ctx;
    s->event_cb = event_cb;
    s->event_arg = event_arg;
    s->next_tx_id = (role == DOKAN_ROLE_HOST) ? 2 : 1;
    *out = s;
    return ESP_OK;
}

void dokan_session_feed(dokan_session_t *s, const uint8_t *data, size_t len) {
    while (len > 0 && !s->error) {
        size_t space = sizeof s->in_buf - s->in_len;
        size_t copy = len < space ? len : space;
        memcpy(s->in_buf + s->in_len, data, copy);
        s->in_len += copy;
        data += copy;
        len -= copy;

        size_t off = 0;
        while (s->in_len - off >= 5) {
            uint8_t type = s->in_buf[off];
            uint16_t sid = rd16(s->in_buf + off + 1);
            uint16_t plen = rd16(s->in_buf + off + 3);
            if (plen > DOKAN_MAX_FRAME_PAYLOAD) { protocol_error(s, ESP_ERR_INVALID_SIZE); break; }
            if (s->in_len - off < (size_t)5 + plen) break;
            process_frame(s, type, sid, s->in_buf + off + 5, plen);
            off += (size_t)5 + plen;
            if (s->error) break;
        }
        if (off > 0) {
            memmove(s->in_buf, s->in_buf + off, s->in_len - off);
            s->in_len -= off;
        }
    }
}

void dokan_session_on_writable(dokan_session_t *s) {
    out_flush(s);
}

esp_err_t dokan_close(dokan_session_t *s) {
    if (!s) return ESP_ERR_INVALID_ARG;
    if (!s->closed && !s->error) {
        uint8_t p[4];
        wr32(p, (uint32_t)ESP_OK);
        enqueue(s, FRAME_GOAWAY, 0, p, 4);
        s->closed = true;
    }
    dokan_stream_t *st = s->streams;
    while (st) {
        dokan_stream_t *n = st->next;
        free(st->rx_buf);
        free(st);
        st = n;
    }
    free(s->out_buf);
    free(s);
    return ESP_OK;
}

esp_err_t dokan_stream_open(dokan_session_t *s, const dokan_stream_opts_t *opts,
                            dokan_stream_t **out) {
    if (!s || !out) return ESP_ERR_INVALID_ARG;
    if (s->closed || s->error) return ESP_ERR_INVALID_STATE;
    if (s->stream_count >= DOKAN_MAX_STREAMS) return ESP_ERR_NO_MEM;
    dokan_stream_t *st = new_stream(s, s->next_tx_id, true);
    if (!st) return ESP_ERR_NO_MEM;
    s->next_tx_id += 2;
    st->send_window = s->window;
    uint8_t p[10];
    wr16(p, opts ? opts->kind : 0);
    wr64(p + 2, opts ? opts->size_hint : 0);
    enqueue(s, FRAME_OPEN, st->id, p, 10);
    *out = st;
    return ESP_OK;
}

esp_err_t dokan_stream_write(dokan_stream_t *st, const void *data, size_t len, size_t *written) {
    if (!st || !written) return ESP_ERR_INVALID_ARG;
    if (!st->local || st->finished_sent || st->reset) return ESP_ERR_INVALID_STATE;
    dokan_session_t *s = st->session;
    uint32_t accept = (len < st->send_window) ? (uint32_t)len : st->send_window;
    size_t off = 0;
    while (off < accept) {
        uint32_t chunk = accept - (uint32_t)off;
        if (chunk > DOKAN_MAX_CHUNK) chunk = DOKAN_MAX_CHUNK;
        enqueue(s, FRAME_DATA, st->id, (const uint8_t *)data + off, (uint16_t)chunk);
        off += chunk;
    }
    st->send_window -= accept;
    *written = accept;
    return ESP_OK;
}

esp_err_t dokan_stream_finish(dokan_stream_t *st) {
    if (!st || !st->local) return ESP_ERR_INVALID_STATE;
    if (st->finished_sent || st->reset) return ESP_ERR_INVALID_STATE;
    enqueue(st->session, FRAME_EOF, st->id, NULL, 0);
    st->finished_sent = true;
    return ESP_OK;
}

esp_err_t dokan_stream_reset(dokan_stream_t *st, esp_err_t reason) {
    if (!st) return ESP_ERR_INVALID_ARG;
    if (st->reset) return ESP_OK;
    uint8_t p[4];
    wr32(p, (uint32_t)reason);
    enqueue(st->session, FRAME_RESET, st->id, p, 4);
    st->reset = true;
    return ESP_OK;
}

esp_err_t dokan_stream_pause(dokan_stream_t *st) {
    if (!st || st->local) return ESP_ERR_INVALID_STATE;
    st->paused = true;
    return ESP_OK;
}

esp_err_t dokan_stream_resume(dokan_stream_t *st) {
    if (!st || st->local) return ESP_ERR_INVALID_STATE;
    if (!st->paused) return ESP_OK;
    st->paused = false;
    dokan_session_t *s = st->session;
    uint16_t id = st->id;
    if (st->rx_len > 0) {
        uint32_t n = (uint32_t)st->rx_len;
        emit(s, DOKAN_EVENT_STREAM_DATA, st, st->rx_buf, st->rx_len, NULL, ESP_OK);
        st = find_stream(s, id);
        if (!st || st->reset) return ESP_OK;
        st->rx_len = 0;
        credit_consume(s, st, n);
        st = find_stream(s, id);
        if (!st || st->reset) return ESP_OK;
    }
    if (st->finished_recv && st->rx_len == 0) deliver_finished(s, st);
    return ESP_OK;
}

void dokan_stream_release(dokan_stream_t *st) {
    if (!st) return;
    dokan_session_t *s = st->session;
    bool open_state = !st->reset && (st->local ? !st->finished_sent : !st->finished_recv);
    if (open_state && !s->closed && !s->error) {
        uint8_t p[4];
        wr32(p, (uint32_t)ESP_OK);
        enqueue(s, FRAME_RESET, st->id, p, 4);
    }
    free_stream(s, st);
}

void dokan_stream_set_user(dokan_stream_t *st, void *user) { if (st) st->user = user; }
void *dokan_stream_get_user(dokan_stream_t *st) { return st ? st->user : NULL; }
