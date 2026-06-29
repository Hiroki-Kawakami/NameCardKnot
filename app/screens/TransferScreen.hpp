/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "widgets.hpp"
#include "SharedCardData.hpp"
#include "NameFont.hpp"
#include "dokan.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

// Parameters handed from the HotKnot phase (ShareScreen / ReceiveScreen) into the
// dokan transfer phase, in RAM (no NVS / restart in between). `own` is this
// device's card to send (null when there is none to offer).
struct TransferStart {
    dokan_role_t role;
    bool offer;     // willing to send `own`
    bool accept;    // wants the peer's card
    char descriptor[DOKAN_DESCRIPTOR_MAX];
    std::shared_ptr<SharedCardData> own;
};

// The data-exchange phase: opens the dokan session from the exchanged descriptor,
// negotiates intent over a HELLO stream, then sends/receives the share-only PDF
// over CARD streams (each direction independent). One concrete class for both
// roles — the protocol is symmetric. Touch is dead until reboot after a HotKnot
// session, so the screen is non-interactive: it shows progress and reboots when
// the exchange ends. dokan events arrive on the transport I/O task; the LVGL poll
// timer reads the published atomics and owns the UI, the file finalize, and close.
class TransferScreen : public NavigationScreen {
public:
    explicit TransferScreen(TransferStart start);
    ~TransferScreen() override;
    void build() override;
    void back() override {}  // non-interactive; reboot is the only exit

private:
    // KIND_ACK is a zero-byte stream meaning "I have fully received your card";
    // a sender waits for it before closing so dokan_close never truncates in-flight
    // data (the send window may hold undelivered bytes — see docs/dokan.md).
    enum StreamKind : uint16_t { KIND_HELLO = 1, KIND_CARD = 2, KIND_ACK = 3 };
    static constexpr uint16_t kHelloVersion = 1;
    static constexpr size_t kChunk = 4096;
    static constexpr size_t kPeerNameMax = 96;

    // dokan event callback (I/O task) -> per-type handlers.
    static void onEvent(const dokan_event_t *ev, void *arg);
    void onConnected();
    void onStreamOpened(dokan_stream_t *st, const dokan_stream_opts_t *opts);
    void onStreamData(dokan_stream_t *st, const uint8_t *data, size_t len);
    void onStreamFinished(dokan_stream_t *st);
    void onStreamWritable(dokan_stream_t *st);
    void sendHello();
    void parsePeerHello();   // decide will_send_/will_recv_ and start CARD send
    void pumpSend();         // feed the outbound CARD stream until the window fills
    void maybeSendAck();     // ack once our inbound is fully received

    void poll();             // LVGL: drive UI, finalize the receive, close, reboot
    bool finalizeReceived(); // parse the temp file, rename to <name>.snc.pdf
    void terminate(bool ok);

    TransferStart start_;
    NameFont name_font_{nullptr};

    lv_obj_t *peer_name_label_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *bar_ = nullptr;
    lv_timer_t *poll_timer_ = nullptr;
    lv_timer_t *reboot_timer_ = nullptr;

    dokan_session_t *session_ = nullptr;

    // I/O-task-only state (no other task touches these while the session lives).
    dokan_stream_t *card_out_ = nullptr;
    std::unique_ptr<SharePdfStream> send_stream_;
    uint8_t send_buf_[kChunk];
    size_t send_buf_len_ = 0, send_buf_off_ = 0;
    uint8_t hello_rx_[256];
    size_t hello_rx_len_ = 0;
    bool hello_sent_ = false;
    bool ack_sent_ = false;
    std::FILE *recv_file_ = nullptr;
    char peer_name_[kPeerNameMax] = {0};  // published before hello_ready_

    // I/O task -> poll thread.
    std::atomic<bool> connected_{false};
    std::atomic<bool> hello_ready_{false};
    std::atomic<bool> will_send_{false};
    std::atomic<bool> will_recv_{false};
    std::atomic<bool> send_done_{false};
    std::atomic<bool> inbound_complete_{false};
    std::atomic<bool> got_peer_ack_{false};
    std::atomic<bool> peer_left_{false};
    std::atomic<bool> failed_{false};
    std::atomic<uint32_t> sent_{0}, recv_{0};
    std::atomic<uint32_t> send_total_{0}, recv_total_{0};

    // poll-thread-only state.
    bool name_shown_ = false;
    bool recv_finalized_ = false;
    bool recv_ok_ = true;
    bool done_marked_ = false;
    uint32_t done_tick_ = 0;
    int last_pct_ = -1;
    uint32_t last_bar_tick_ = 0;
    bool terminating_ = false;
    bool closed_ = false;
    std::string saved_name_;
};
