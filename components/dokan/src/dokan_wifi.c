/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * WiFi P2P transport (device): HOST raises a SoftAP and listens, CLIENT joins as
 * STA with a static IP and connects. SSID/PSK derive from the descriptor seed;
 * IPs are fixed private addresses (no DHCP). Mirrors dokan_sim.c's I/O loop with
 * non-blocking sends + the outbound-queue/on_writable path.
 */

#ifdef ESP_PLATFORM

#include "dokan_transport.h"
#include "dokan_wifi_params.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#define DOKAN_WIFI_AP_IP   "192.168.4.1"   /* SoftAP gateway (ESP-IDF default subnet) */
#define DOKAN_WIFI_STA_IP  "192.168.4.2"   /* STA static address - no DHCP */
#define DOKAN_WIFI_NETMASK "255.255.255.0"

#define WIFI_TASK_STACK   8192   /* deep chain: recv -> feed -> app cb -> lwip send */
#define WIFI_TASK_PRIO    5
#define WIFI_RX_BUF       2048   /* heap, not stack */
#define SELECT_TIMEOUT_MS 20     /* poll stop_requested / want_writable */

#define BIT_LINK_UP BIT0
#define BIT_STOP    BIT1

typedef struct {
    dokan_transport_t base;
    dokan_transport_host_t host;
    dokan_role_t role;
    dokan_wifi_params_t p;
    char ssid[DOKAN_WIFI_SSID_MAX];
    char psk[DOKAN_WIFI_PSK_MAX];
    uint32_t connect_timeout_ms;
    esp_netif_t *netif;
    esp_event_handler_instance_t wifi_h;
    EventGroupHandle_t events;
    SemaphoreHandle_t done;
    TaskHandle_t task;
    int datafd, listenfd;
    volatile bool want_writable, stop_requested;
} dokan_wifi_t;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)base; (void)data;
    dokan_wifi_t *s = arg;
    switch (id) {
    case WIFI_EVENT_AP_START:
    case WIFI_EVENT_STA_CONNECTED:
        xEventGroupSetBits(s->events, BIT_LINK_UP);
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        xEventGroupClearBits(s->events, BIT_LINK_UP);
        if (!s->stop_requested) esp_wifi_connect();  /* peer AP may not be up yet */
        break;
    default:
        break;
    }
}

static void ensure_globals(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();  /* ESP_ERR_INVALID_STATE if already up - fine */
}

static esp_err_t wifi_bringup(dokan_wifi_t *s) {
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) return ESP_FAIL;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, s, &s->wifi_h);

    if (s->role == DOKAN_ROLE_HOST) {
        s->netif = esp_netif_create_default_wifi_ap();
        wifi_config_t wc = {0};
        size_t n = strlen(s->ssid);
        memcpy(wc.ap.ssid, s->ssid, n);
        wc.ap.ssid_len = (uint8_t)n;
        memcpy(wc.ap.password, s->psk, strnlen(s->psk, sizeof s->psk - 1));
        wc.ap.channel = s->p.channel;
        wc.ap.max_connection = 1;
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
        if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) return ESP_FAIL;
        if (esp_wifi_set_config(WIFI_IF_AP, &wc) != ESP_OK) return ESP_FAIL;
    } else {
        s->netif = esp_netif_create_default_wifi_sta();
        esp_netif_dhcpc_stop(s->netif);  /* static IP, skip DHCP */
        esp_netif_ip_info_t ip = {0};
        ip.ip.addr = ipaddr_addr(DOKAN_WIFI_STA_IP);
        ip.gw.addr = ipaddr_addr(DOKAN_WIFI_AP_IP);
        ip.netmask.addr = ipaddr_addr(DOKAN_WIFI_NETMASK);
        esp_netif_set_ip_info(s->netif, &ip);
        wifi_config_t wc = {0};
        memcpy(wc.sta.ssid, s->ssid, strnlen(s->ssid, sizeof wc.sta.ssid - 1));
        memcpy(wc.sta.password, s->psk, strnlen(s->psk, sizeof s->psk - 1));
        wc.sta.channel = s->p.channel;
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return ESP_FAIL;
        if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) return ESP_FAIL;
    }
    if (esp_wifi_start() != ESP_OK) return ESP_FAIL;
    if (s->role == DOKAN_ROLE_CLIENT) esp_wifi_connect();
    return ESP_OK;
}

static void wifi_teardown_radio(dokan_wifi_t *s) {
    esp_wifi_stop();
    if (s->wifi_h) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s->wifi_h);
        s->wifi_h = NULL;
    }
    esp_wifi_deinit();
    if (s->netif) { esp_netif_destroy_default_wifi(s->netif); s->netif = NULL; }
    if (s->events) { vEventGroupDelete(s->events); s->events = NULL; }
    if (s->done) { vSemaphoreDelete(s->done); s->done = NULL; }
}

/* TCP rendezvous over the established link; returns fd or -1 on stop/timeout. */
static int wifi_rendezvous(dokan_wifi_t *s) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s->p.port);

    if (s->role == DOKAN_ROLE_HOST) {
        s->listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (s->listenfd < 0) return -1;
        int yes = 1;
        setsockopt(s->listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s->listenfd, (struct sockaddr *)&addr, sizeof addr) != 0) return -1;
        if (listen(s->listenfd, 1) != 0) return -1;
        for (;;) {
            if (s->stop_requested) return -1;
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(s->listenfd, &rf);
            struct timeval tv = { 0, SELECT_TIMEOUT_MS * 1000 };
            int r = select(s->listenfd + 1, &rf, NULL, NULL, &tv);
            if (r > 0 && FD_ISSET(s->listenfd, &rf)) return accept(s->listenfd, NULL, NULL);
        }
    }

    addr.sin_addr.s_addr = ipaddr_addr(DOKAN_WIFI_AP_IP);
    uint32_t waited = 0;
    for (;;) {
        if (s->stop_requested) return -1;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) return fd;
        if (fd >= 0) close(fd);
        if (waited >= s->connect_timeout_ms) return -1;
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }
}

static void wifi_task(void *arg) {
    dokan_wifi_t *s = arg;
    const dokan_transport_host_t *h = &s->host;
    h->bind_task(h->ctx);

    EventBits_t b = xEventGroupWaitBits(s->events, BIT_LINK_UP | BIT_STOP,
                                        pdFALSE, pdFALSE, pdMS_TO_TICKS(s->connect_timeout_ms));
    int fd = -1;
    if ((b & BIT_LINK_UP) && !s->stop_requested) fd = wifi_rendezvous(s);

    bool deferred = false;
    uint8_t *buf = (fd >= 0) ? malloc(WIFI_RX_BUF) : NULL;
    if (fd < 0 || !buf) {
        if (fd >= 0) close(fd);
        if (!s->stop_requested) h->on_error(h->ctx, fd < 0 ? ESP_ERR_TIMEOUT : ESP_ERR_NO_MEM);
    } else {
        s->datafd = fd;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        h->on_connected(h->ctx);
        bool peer_gone = false;
        for (;;) {
            fd_set rf, wf;
            FD_ZERO(&rf);
            FD_ZERO(&wf);
            int mx = s->datafd;
            if (!peer_gone) FD_SET(s->datafd, &rf);
            if (s->want_writable) FD_SET(s->datafd, &wf);
            struct timeval tv = { 0, SELECT_TIMEOUT_MS * 1000 };
            int r = select(mx + 1, &rf, &wf, NULL, &tv);
            if (s->stop_requested) break;
            if (r > 0) {
                if (FD_ISSET(s->datafd, &wf)) h->on_writable(h->ctx);
                if (!peer_gone && FD_ISSET(s->datafd, &rf)) {
                    int n = recv(s->datafd, buf, WIFI_RX_BUF, 0);
                    if (n == 0) { h->on_error(h->ctx, ESP_FAIL); peer_gone = true; }
                    else if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) { h->on_error(h->ctx, ESP_FAIL); peer_gone = true; } }
                    else h->on_bytes(h->ctx, buf, (size_t)n);
                }
            }
            if (h->poll_close(h->ctx)) { deferred = true; break; }
        }
        free(buf);
    }

    if (s->datafd >= 0) { close(s->datafd); s->datafd = -1; }
    if (s->listenfd >= 0) { close(s->listenfd); s->listenfd = -1; }
    if (deferred) {
        wifi_teardown_radio(s);
        h->on_closed(h->ctx);     /* frees the session + this transport */
    } else {
        xSemaphoreGive(s->done);  /* stop() waits, then tears the radio down */
    }
    vTaskDelete(NULL);
}

static esp_err_t wifi_start(dokan_transport_t *self, dokan_role_t role,
                            const char app_id[DOKAN_APP_ID_LEN],
                            const uint8_t *params, size_t plen,
                            const dokan_config_t *cfg, const dokan_transport_host_t *host) {
    dokan_wifi_t *s = (dokan_wifi_t *)self;
    if (dokan_wifi_params_decode(params, plen, &s->p) != ESP_OK) return ESP_ERR_INVALID_ARG;
    s->role = role;
    s->host = *host;
    s->connect_timeout_ms = (cfg && cfg->connect_timeout_ms) ? cfg->connect_timeout_ms : 10000;
    dokan_wifi_derive(app_id, s->p.seed, s->ssid, s->psk);

    ensure_globals();
    s->events = xEventGroupCreate();
    s->done = xSemaphoreCreateBinary();
    if (!s->events || !s->done) { wifi_teardown_radio(s); return ESP_ERR_NO_MEM; }

    if (wifi_bringup(s) != ESP_OK) { wifi_teardown_radio(s); return ESP_FAIL; }
    if (xTaskCreate(wifi_task, "dokan_wifi", WIFI_TASK_STACK, s, WIFI_TASK_PRIO, &s->task) != pdPASS) {
        wifi_teardown_radio(s);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static size_t wifi_write(dokan_transport_t *self, const uint8_t *data, size_t len) {
    dokan_wifi_t *s = (dokan_wifi_t *)self;
    if (s->datafd < 0) return 0;
    size_t off = 0;
    while (off < len) {
        int n = send(s->datafd, data + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;  /* EAGAIN/EWOULDBLOCK or error: leave the rest for on_writable */
        }
        off += (size_t)n;
    }
    s->want_writable = (off < len);  /* the select-timeout loop picks this up */
    return off;
}

static void wifi_stop(dokan_transport_t *self) {
    dokan_wifi_t *s = (dokan_wifi_t *)self;
    s->stop_requested = true;
    if (s->events) xEventGroupSetBits(s->events, BIT_STOP);  /* abort the link wait */
    if (s->task) xSemaphoreTake(s->done, portMAX_DELAY);     /* task gave done at exit */
    wifi_teardown_radio(s);
}

esp_err_t dokan_wifi_transport_create(dokan_transport_t **out) {
    dokan_wifi_t *s = calloc(1, sizeof *s);
    if (!s) return ESP_ERR_NO_MEM;
    s->base.id = DOKAN_TRANSPORT_WIFI;
    s->base.start = wifi_start;
    s->base.write = wifi_write;
    s->base.stop = wifi_stop;
    s->datafd = s->listenfd = -1;
    *out = &s->base;
    return ESP_OK;
}

#endif /* ESP_PLATFORM */
