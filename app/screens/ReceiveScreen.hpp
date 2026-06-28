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

private:
    bool return_my_data_ = false;

    void loadShareCardData();
};
