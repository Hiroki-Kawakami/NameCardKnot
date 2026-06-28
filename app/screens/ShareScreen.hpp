/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "TransferScreen.hpp"
#include "dokan.h"

class ShareScreen : public TransferScreen {
public:
    using TransferScreen::TransferScreen;
    void build() override;

protected:
    const char *transferTitle() const override { return "Share"; }
    void onHotKnotReady() override;
    void onHotKnotDone() override;

private:
    bool allow_return_data_ = false;
    char descriptor_[DOKAN_DESCRIPTOR_MAX] = {0};
};
