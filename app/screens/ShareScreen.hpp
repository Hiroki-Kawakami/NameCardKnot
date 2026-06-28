/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "TransferScreen.hpp"

class ShareScreen : public TransferScreen {
public:
    using TransferScreen::TransferScreen;
    void build() override;

private:
    bool allow_return_data_ = false;
};
