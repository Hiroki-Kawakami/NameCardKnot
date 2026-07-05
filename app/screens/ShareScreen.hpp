/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "HotKnotScreen.hpp"
#include "dokan.h"

class ShareScreen : public HotKnotScreen {
public:
    using HotKnotScreen::HotKnotScreen;
    void build() override;

protected:
    const char *transferTitle() const override { return S().share; }
    void onHotKnotReady() override;
    void onHotKnotDone() override;
    bootmsg::Id bootMsgId() const override { return bootmsg::Id::ShareFailed; }

private:
    bool allow_return_data_ = false;
    char descriptor_[DOKAN_DESCRIPTOR_MAX] = {0};
};
