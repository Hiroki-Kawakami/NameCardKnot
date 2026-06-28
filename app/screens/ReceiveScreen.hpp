/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "TransferScreen.hpp"

class ReceiveScreen : public TransferScreen {
public:
    using TransferScreen::TransferScreen;
    void build() override;

protected:
    const char *transferTitle() const override { return "Receive"; }
    void stashReceived(const uint8_t *data, size_t len) override;
    void onHotKnotDone() override;

private:
    bool return_my_data_ = false;
    char descriptor_[BSP_HOTKNOT_MAX_PAYLOAD] = {0};

    void loadShareCardData();
};
