/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "HotKnotScreen.hpp"

class ReceiveScreen : public HotKnotScreen {
public:
    using HotKnotScreen::HotKnotScreen;
    void build() override;

protected:
    const char *transferTitle() const override { return "Receive"; }
    void stashReceived(const uint8_t *data, size_t len) override;
    void onHotKnotDone() override;
    bootmsg::Id bootMsgId() const override { return bootmsg::Id::ReceiveFailed; }

private:
    bool return_my_data_ = false;
    char descriptor_[BSP_HOTKNOT_MAX_PAYLOAD] = {0};

    void loadShareCardData();
};
