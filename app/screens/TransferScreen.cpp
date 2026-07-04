/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "TransferScreen.hpp"
#include "NameCardKnot.hpp"
#include "LastCard.hpp"
#include "Power.hpp"

#include "esp_log.h"
#include <cstring>
#include <sys/stat.h>

static const char *TAG = "transfer";

static constexpr char kTempPath[] = RECEIVED_CARDS_DIR "/.nck_transfer";
static constexpr uint32_t kBarRefreshMs = 700;
static constexpr uint32_t kAckTimeoutMs = 5000;  // fallback if the peer's ack never arrives
static constexpr uint32_t kRebootDelayMs = 4000; // show the result, then reboot

static bool file_exists(const std::string &path) {
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (f) { std::fclose(f); return true; }
    return false;
}

// FAT-safe single path component from a UTF-8 card name (non-ASCII bytes kept).
static std::string sanitize_filename(const std::string &name) {
    std::string out;
    for (unsigned char c : name) {
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            out += '_';
        else
            out += (char)c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out;
}

TransferScreen::TransferScreen(TransferStart start) : start_(std::move(start)) {}

TransferScreen::~TransferScreen() {
    if (session_ && !closed_) dokan_close(session_);
    if (recv_file_) std::fclose(recv_file_);
    if (poll_timer_) lv_timer_delete(poll_timer_);
    if (reboot_timer_) lv_timer_delete(reboot_timer_);
}

void TransferScreen::onAppear() {
    power::set_timeout(this, 0);
}

void TransferScreen::build() {
    createNavigation("Transfer");
    lv_obj_set_style_border_width(navigation_, 0, 0);
    lv_obj_set_flex_align(contents_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(contents_, 40, 0);
    lv_obj_set_style_pad_row(contents_, 24, 0);

    peer_name_label_ = lv_label_create(contents_);
    lv_obj_set_width(peer_name_label_, LV_PCT(100));
    lv_label_set_long_mode(peer_name_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(peer_name_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(peer_name_label_, name_font_.font(), 0);
    lv_label_set_text(peer_name_label_, "");
    lv_obj_add_flag(peer_name_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(contents_);
    lv_obj_set_width(status_label_, LV_PCT(100));
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_24, 0);
    lv_label_set_text(status_label_, "Connecting...");

    bar_ = lv_bar_create(contents_);
    lv_obj_set_width(bar_, LV_PCT(80));
    lv_bar_set_range(bar_, 0, 100);
    lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
    lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);

    mount_sd_card();  // the receive direction writes the incoming card here
    mkdir(RECEIVED_CARDS_DIR, 0777);  // collect received cards together (ok if it exists)

    dokan_config_t cfg = {};
    cfg.connect_timeout_ms = 20000;
    esp_err_t err = dokan_open(start_.descriptor, start_.role, DOKAN_APP_ID, &cfg,
                               onEvent, this, &session_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "dokan_open failed: %s", esp_err_to_name(err));
        failed_.store(true, std::memory_order_release);
    }

    poll_timer_ = lv_timer_create([](lv_timer_t *t) {
        static_cast<TransferScreen *>(lv_timer_get_user_data(t))->poll();
    }, 100, this);
}

// MARK: dokan events (transport I/O task)

void TransferScreen::onEvent(const dokan_event_t *ev, void *arg) {
    auto *self = static_cast<TransferScreen *>(arg);
    switch (ev->type) {
        case DOKAN_EVENT_CONNECTED:       self->onConnected(); break;
        case DOKAN_EVENT_STREAM_OPENED:   self->onStreamOpened(ev->stream, &ev->opts); break;
        case DOKAN_EVENT_STREAM_DATA:     self->onStreamData(ev->stream, ev->data, ev->len); break;
        case DOKAN_EVENT_STREAM_FINISHED: self->onStreamFinished(ev->stream); break;
        case DOKAN_EVENT_STREAM_WRITABLE: self->onStreamWritable(ev->stream); break;
        case DOKAN_EVENT_DISCONNECTED:    self->peer_left_.store(true, std::memory_order_release); break;
        case DOKAN_EVENT_STREAM_RESET:
        case DOKAN_EVENT_ERROR:           self->failed_.store(true, std::memory_order_release); break;
    }
}

void TransferScreen::onConnected() {
    connected_.store(true, std::memory_order_release);
    sendHello();
}

void TransferScreen::sendHello() {
    if (hello_sent_) return;
    hello_sent_ = true;

    const std::string &name = start_.own ? start_.own->name() : std::string();
    uint16_t name_len = (uint16_t)name.size();
    if (name_len > 200) name_len = 200;

    uint8_t buf[6 + 200];
    buf[0] = (uint8_t)(kHelloVersion & 0xff);
    buf[1] = (uint8_t)(kHelloVersion >> 8);
    buf[2] = start_.offer ? 1 : 0;
    buf[3] = start_.accept ? 1 : 0;
    buf[4] = (uint8_t)(name_len & 0xff);
    buf[5] = (uint8_t)(name_len >> 8);
    memcpy(buf + 6, name.data(), name_len);
    size_t total = 6 + name_len;

    dokan_stream_opts_t opts = { KIND_HELLO, total };
    dokan_stream_t *st = nullptr;
    if (dokan_stream_open(session_, &opts, &st) != ESP_OK) {
        failed_.store(true, std::memory_order_release);
        return;
    }
    size_t off = 0;
    for (int guard = 0; guard < 64 && off < total; guard++) {
        size_t w = 0;
        dokan_stream_write(st, buf + off, total - off, &w);
        off += w;
        if (w == 0) break;  // HELLO fits the initial window; a stall means trouble
    }
    dokan_stream_finish(st);
}

void TransferScreen::onStreamOpened(dokan_stream_t *st, const dokan_stream_opts_t *opts) {
    dokan_stream_set_user(st, (void *)(intptr_t)opts->kind);
    if (opts->kind == KIND_CARD) {
        recv_total_.store((uint32_t)opts->size_hint, std::memory_order_relaxed);
        recv_file_ = std::fopen(kTempPath, "wb");
        if (!recv_file_) failed_.store(true, std::memory_order_release);
    } else if (opts->kind == KIND_ACK) {
        got_peer_ack_.store(true, std::memory_order_release);  // peer has our card
    }
}

void TransferScreen::onStreamData(dokan_stream_t *st, const uint8_t *data, size_t len) {
    auto kind = (uint16_t)(intptr_t)dokan_stream_get_user(st);
    if (kind == KIND_HELLO) {
        if (hello_rx_len_ + len > sizeof hello_rx_) len = sizeof hello_rx_ - hello_rx_len_;
        memcpy(hello_rx_ + hello_rx_len_, data, len);
        hello_rx_len_ += len;
    } else if (kind == KIND_CARD && recv_file_) {
        if (std::fwrite(data, 1, len, recv_file_) != len)
            failed_.store(true, std::memory_order_release);
        recv_.fetch_add((uint32_t)len, std::memory_order_relaxed);
    }
}

void TransferScreen::onStreamFinished(dokan_stream_t *st) {
    auto kind = (uint16_t)(intptr_t)dokan_stream_get_user(st);
    if (kind == KIND_HELLO) {
        parsePeerHello();
    } else if (kind == KIND_CARD) {
        if (recv_file_) { std::fclose(recv_file_); recv_file_ = nullptr; }
        inbound_complete_.store(true, std::memory_order_release);
        maybeSendAck();
    }
}

void TransferScreen::onStreamWritable(dokan_stream_t *st) {
    if (st == card_out_) pumpSend();
}

void TransferScreen::parsePeerHello() {
    bool peer_offer = false, peer_accept = false;
    if (hello_rx_len_ >= 6) {
        peer_offer = hello_rx_[2] != 0;
        peer_accept = hello_rx_[3] != 0;
        size_t name_len = hello_rx_[4] | (hello_rx_[5] << 8);
        if (6 + name_len > hello_rx_len_) name_len = hello_rx_len_ - 6;
        if (name_len > kPeerNameMax - 1) name_len = kPeerNameMax - 1;
        memcpy(peer_name_, hello_rx_ + 6, name_len);
        peer_name_[name_len] = '\0';
    }

    bool will_send = start_.offer && peer_accept && start_.own && start_.own->valid();
    bool will_recv = peer_offer && start_.accept;
    will_recv_.store(will_recv, std::memory_order_relaxed);

    if (will_send) {
        send_stream_ = start_.own->share_stream();
        if (send_stream_ && send_stream_->valid()) {
            send_total_.store((uint32_t)send_stream_->size(), std::memory_order_relaxed);
            dokan_stream_opts_t opts = { KIND_CARD, send_stream_->size() };
            if (dokan_stream_open(session_, &opts, &card_out_) != ESP_OK) will_send = false;
        } else {
            will_send = false;
        }
    }
    will_send_.store(will_send, std::memory_order_relaxed);
    hello_ready_.store(true, std::memory_order_release);

    if (will_send && card_out_) pumpSend();
    maybeSendAck();  // nothing to receive -> ack immediately
}

void TransferScreen::maybeSendAck() {
    if (ack_sent_ || !hello_ready_.load(std::memory_order_relaxed)) return;
    bool inbound_done = !will_recv_.load(std::memory_order_relaxed) ||
                        inbound_complete_.load(std::memory_order_relaxed);
    if (!inbound_done) return;
    ack_sent_ = true;
    dokan_stream_opts_t opts = { KIND_ACK, 0 };
    dokan_stream_t *st = nullptr;
    if (dokan_stream_open(session_, &opts, &st) == ESP_OK) dokan_stream_finish(st);
}

void TransferScreen::pumpSend() {
    for (;;) {
        if (send_buf_off_ == send_buf_len_) {
            send_buf_len_ = send_stream_->read(send_buf_, kChunk);
            send_buf_off_ = 0;
            if (send_buf_len_ == 0) {
                if (send_stream_->remaining() > 0 || send_stream_->error()) {
                    failed_.store(true, std::memory_order_release);
                    return;
                }
                dokan_stream_finish(card_out_);
                send_done_.store(true, std::memory_order_release);
                return;
            }
        }
        size_t w = 0;
        dokan_stream_write(card_out_, send_buf_ + send_buf_off_, send_buf_len_ - send_buf_off_, &w);
        send_buf_off_ += w;
        sent_.fetch_add((uint32_t)w, std::memory_order_relaxed);
        if (w == 0) return;  // window full: resume on STREAM_WRITABLE
    }
}

// MARK: UI / lifecycle (LVGL thread)

void TransferScreen::poll() {
    if (terminating_) return;
    if (!connected_.load(std::memory_order_acquire)) {
        if (failed_.load(std::memory_order_acquire)) terminate(false);  // connect failed/timeout
        return;
    }
    if (!hello_ready_.load(std::memory_order_acquire)) {
        // Error or peer drop during the handshake.
        if (failed_.load(std::memory_order_acquire) || peer_left_.load(std::memory_order_acquire))
            terminate(false);
        return;
    }
    // Note: the failed_/peer_left_ terminal checks come AFTER the done test below, so
    // the session teardown that follows a successful exchange (the peer closing, seen
    // here as an error/disconnect) never overrides a completed transfer.

    if (!name_shown_) {
        name_shown_ = true;
        epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
        if (will_recv_.load(std::memory_order_relaxed) && peer_name_[0]) {
            lv_label_set_text(peer_name_label_, peer_name_);
            lv_obj_remove_flag(peer_name_label_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(status_label_, "Exchanging cards...");
        lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    }

    if (will_recv_.load(std::memory_order_relaxed) &&
        inbound_complete_.load(std::memory_order_acquire) && !recv_finalized_) {
        recv_finalized_ = true;
        recv_ok_ = finalizeReceived();
    }

    uint32_t total = send_total_.load(std::memory_order_relaxed) +
                     recv_total_.load(std::memory_order_relaxed);
    if (total > 0) {
        uint32_t cur = sent_.load(std::memory_order_relaxed) + recv_.load(std::memory_order_relaxed);
        int pct = (int)((uint64_t)cur * 100 / total);
        if (pct > 100) pct = 100;
        if (pct != last_pct_ && (last_pct_ < 0 || pct == 100 ||
                                 lv_tick_elaps(last_bar_tick_) >= kBarRefreshMs)) {
            epd_set_next_refresh_mode(BSP_EPD_MODE_FAST);
            lv_bar_set_value(bar_, pct, LV_ANIM_OFF);
            last_pct_ = pct;
            last_bar_tick_ = lv_tick_get();
        }
    }

    bool send_ok = !will_send_.load(std::memory_order_relaxed) || send_done_.load(std::memory_order_acquire);
    bool recv_ok = !will_recv_.load(std::memory_order_relaxed) || recv_finalized_;
    if (send_ok && recv_ok) {
        if (!done_marked_) {
            done_marked_ = true;
            done_tick_ = lv_tick_get();
            epd_set_next_refresh_mode(BSP_EPD_MODE_FAST);
            lv_bar_set_value(bar_, 100, LV_ANIM_OFF);
        }
        // Close only once the peer has acked our card (so dokan_close can't drop
        // in-flight send-window bytes); fall back on a timeout / the peer leaving.
        bool peer_has_our_card = !will_send_.load(std::memory_order_relaxed) ||
                                 got_peer_ack_.load(std::memory_order_acquire);
        if (peer_has_our_card ||
            peer_left_.load(std::memory_order_acquire) ||
            failed_.load(std::memory_order_acquire) ||
            lv_tick_elaps(done_tick_) >= kAckTimeoutMs)
            terminate(recv_ok_);
        return;
    }

    // Errored or peer gone: only a failure if bytes are still missing. If all bytes
    // moved and just the finalize is pending, fall through — the next tick finalizes
    // and the done branch above closes cleanly.
    bool inbound_short = will_recv_.load(std::memory_order_relaxed) &&
                         !inbound_complete_.load(std::memory_order_acquire);
    bool outbound_short = will_send_.load(std::memory_order_relaxed) &&
                          !send_done_.load(std::memory_order_acquire);
    if ((failed_.load(std::memory_order_acquire) || peer_left_.load(std::memory_order_acquire)) &&
        (inbound_short || outbound_short))
        terminate(false);
}

bool TransferScreen::finalizeReceived() {
    std::string name;
    {
        auto data = SharedCardData::open(kTempPath);
        if (data && data->valid()) name = data->name();
    }
    if (name.empty() && !file_exists(kTempPath)) return false;

    std::string base = sanitize_filename(name);
    if (base.empty()) base = "card";
    std::string dir = RECEIVED_CARDS_DIR "/";
    std::string dest = dir + base + ".snc.pdf";
    for (int n = 1; file_exists(dest); n++)
        dest = dir + base + "_" + std::to_string(n) + ".snc.pdf";

    if (std::rename(kTempPath, dest.c_str()) != 0) {
        std::remove(kTempPath);
        return false;
    }
    lastcard::save_received(dest);
    saved_name_ = name;
    return true;
}

void TransferScreen::terminate(bool ok) {
    if (terminating_) return;
    terminating_ = true;
    if (poll_timer_) { lv_timer_delete(poll_timer_); poll_timer_ = nullptr; }

    if (session_ && !closed_) { dokan_close(session_); closed_ = true; }
    if (recv_file_) { std::fclose(recv_file_); recv_file_ = nullptr; }
    if (!ok) std::remove(kTempPath);  // drop a partial / invalid receive

    epd_set_next_refresh_mode(BSP_EPD_MODE_QUALITY);
    lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
    if (ok) {
        if (!saved_name_.empty()) {
            lv_label_set_text_fmt(status_label_, "Received %s.", saved_name_.c_str());
        } else {
            lv_label_set_text(status_label_, "Done.");
        }
    } else {
        lv_obj_add_flag(peer_name_label_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status_label_, "Transfer failed.");
    }

    reboot_timer_ = lv_timer_create([](lv_timer_t *) { bsp_hw_reset(); }, kRebootDelayMs, this);
    lv_timer_set_repeat_count(reboot_timer_, 1);
}
