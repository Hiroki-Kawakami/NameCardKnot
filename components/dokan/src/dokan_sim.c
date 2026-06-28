/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Host-only loopback transport: two sessions rendezvous on a 127.0.0.1 port
 * derived from the shared seed (HOST listens, CLIENT connects). One pthread per
 * session runs the connect/recv loop; writes are blocking sends. Lets the whole
 * runtime be exercised end-to-end without WiFi.
 */

#ifndef ESP_PLATFORM

#include "dokan_transport.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

typedef struct {
    dokan_transport_t base;
    dokan_transport_host_t host;
    dokan_role_t role;
    uint16_t port;
    uint32_t connect_timeout_ms;
    int datafd;
    int listenfd;
    int wake[2];
    pthread_t task;
    bool task_started;
    volatile bool stop_requested;
} dokan_sim_t;

static uint16_t port_from_seed(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return (uint16_t)(20000 + (h % 20000));
}

static void drain_pipe(int fd) {
    uint8_t b[64];
    while (read(fd, b, sizeof b) > 0) { }
}

/* Establish the data socket; returns fd or -1 on stop/timeout. */
static int sim_rendezvous(dokan_sim_t *s) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (s->role == DOKAN_ROLE_HOST) {
        for (;;) {
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(s->listenfd, &rf);
            FD_SET(s->wake[0], &rf);
            int mx = s->listenfd > s->wake[0] ? s->listenfd : s->wake[0];
            struct timeval tv = { (long)(s->connect_timeout_ms / 1000),
                                  (long)((s->connect_timeout_ms % 1000) * 1000) };
            int r = select(mx + 1, &rf, NULL, NULL, &tv);
            if (r <= 0) return -1;
            if (FD_ISSET(s->wake[0], &rf)) { drain_pipe(s->wake[0]); if (s->stop_requested) return -1; }
            if (FD_ISSET(s->listenfd, &rf)) return accept(s->listenfd, NULL, NULL);
        }
    }

    /* CLIENT: retry connect until the host is listening, or stop/timeout. */
    uint32_t waited = 0;
    for (;;) {
        if (s->stop_requested) return -1;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) return fd;
        if (fd >= 0) close(fd);
        if (waited >= s->connect_timeout_ms) return -1;
        usleep(20000);
        waited += 20;
    }
}

static void sim_close_fds(dokan_sim_t *s) {
    if (s->datafd >= 0)   { close(s->datafd);   s->datafd = -1; }
    if (s->listenfd >= 0) { close(s->listenfd); s->listenfd = -1; }
    if (s->wake[0] >= 0)  { close(s->wake[0]);  s->wake[0] = -1; }
    if (s->wake[1] >= 0)  { close(s->wake[1]);  s->wake[1] = -1; }
}

static void *sim_task(void *arg) {
    dokan_sim_t *s = arg;
    const dokan_transport_host_t *h = &s->host;
    h->bind_task(h->ctx);

    int fd = sim_rendezvous(s);
    bool deferred = false;
    if (fd < 0) {
        if (!s->stop_requested) h->on_error(h->ctx, ESP_ERR_TIMEOUT);
    } else {
        s->datafd = fd;
        h->on_connected(h->ctx);
        uint8_t buf[4096];
        bool peer_gone = false;
        /* Exit only on stop or a deferred close, so a cross-thread stop() always
         * joins a task parked in select() (no self-exit-vs-stop race). */
        for (;;) {
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(s->wake[0], &rf);
            int mx = s->wake[0];
            if (!peer_gone) { FD_SET(s->datafd, &rf); if (s->datafd > mx) mx = s->datafd; }
            if (select(mx + 1, &rf, NULL, NULL, NULL) <= 0) continue;
            if (FD_ISSET(s->wake[0], &rf)) drain_pipe(s->wake[0]);
            if (s->stop_requested) break;
            if (!peer_gone && FD_ISSET(s->datafd, &rf)) {
                ssize_t n = recv(s->datafd, buf, sizeof buf, 0);
                if (n <= 0) { h->on_error(h->ctx, ESP_FAIL); peer_gone = true; }
                else h->on_bytes(h->ctx, buf, (size_t)n);
            }
            if (h->poll_close(h->ctx)) { deferred = true; break; }
        }
    }

    sim_close_fds(s);
    if (deferred) {
        pthread_detach(pthread_self());
        h->on_closed(h->ctx);
    }
    return NULL;
}

static esp_err_t sim_start(dokan_transport_t *self, dokan_role_t role,
                           const uint8_t *params, size_t plen,
                           const dokan_config_t *cfg, const dokan_transport_host_t *host) {
    dokan_sim_t *s = (dokan_sim_t *)self;
    s->role = role;
    s->host = *host;
    s->port = port_from_seed(params, plen);
    s->connect_timeout_ms = (cfg && cfg->connect_timeout_ms) ? cfg->connect_timeout_ms : 5000;

    if (pipe(s->wake) != 0) return ESP_FAIL;
    fcntl(s->wake[0], F_SETFL, O_NONBLOCK);  /* drain_pipe must not block */
    fcntl(s->wake[1], F_SETFL, O_NONBLOCK);

    if (role == DOKAN_ROLE_HOST) {
        s->listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (s->listenfd < 0) return ESP_FAIL;
        int yes = 1;
        setsockopt(s->listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_port = htons(s->port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s->listenfd, (struct sockaddr *)&addr, sizeof addr) != 0) return ESP_FAIL;
        if (listen(s->listenfd, 1) != 0) return ESP_FAIL;
    }

    if (pthread_create(&s->task, NULL, sim_task, s) != 0) return ESP_FAIL;
    s->task_started = true;
    return ESP_OK;
}

static size_t sim_write(dokan_transport_t *self, const uint8_t *data, size_t len) {
    dokan_sim_t *s = (dokan_sim_t *)self;
    if (s->datafd < 0) return 0;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(s->datafd, data + off, len - off, 0);
        if (n < 0) { if (errno == EINTR) continue; break; }
        off += (size_t)n;
    }
    return off;
}

static void sim_stop(dokan_transport_t *self) {
    dokan_sim_t *s = (dokan_sim_t *)self;
    s->stop_requested = true;
    if (s->wake[1] >= 0) { uint8_t b = 1; ssize_t r = write(s->wake[1], &b, 1); (void)r; }
    if (s->task_started) pthread_join(s->task, NULL);
}

esp_err_t dokan_sim_transport_create(dokan_transport_t **out) {
    signal(SIGPIPE, SIG_IGN);  /* a send to a peer that closed must not kill us */
    dokan_sim_t *s = calloc(1, sizeof *s);
    if (!s) return ESP_ERR_NO_MEM;
    s->base.id = DOKAN_TRANSPORT_SIM;
    s->base.start = sim_start;
    s->base.write = sim_write;
    s->base.stop = sim_stop;
    s->datafd = s->listenfd = -1;
    s->wake[0] = s->wake[1] = -1;
    *out = &s->base;
    return ESP_OK;
}

#endif /* !ESP_PLATFORM */
