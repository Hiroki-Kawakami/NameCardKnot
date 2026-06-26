/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 *
 * Host test for NameCardData (no ESP-IDF, no LVGL): loads a .mnc.pdf and a plain
 * image via the same async loader (synchronous on the host) and checks
 * detection, the decoded display image, and metadata against the namecard_pdf
 * golden fixtures + assets. Run via run.sh.
 */

#include <cstdio>
#include <string>

#include "NameCardData.hpp"

static int failures = 0;
#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("FAIL: %s\n", msg);                             \
            failures++;                                             \
        }                                                           \
    } while (0)

static imgproc::Options display_opts() {
    imgproc::Options o;
    o.target_w = 540;
    o.target_h = 960;
    o.fit = imgproc::Fit::Contain;
    o.levels = 16;
    return o;
}

int main(int argc, char **argv) {
    std::string fixtures = argc > 1 ? argv[1] : "fixtures";
    std::string assets = argc > 2 ? argv[2] : "assets";
    auto opts = display_opts();

    // .mnc.pdf -> Card: display image decoded from its embedded byte range.
    {
        auto d = NameCardData::load(fixtures + "/basic.mnc.pdf", opts);
        CHECK(d->state() == NameCardData::State::Ok, "mnc state Ok");
        CHECK(d->kind() == NameCardData::Kind::Card, "mnc is Card");
        CHECK(d->is_card(), "mnc is_card");
        CHECK(d->name() == "山田太郎", "mnc name");
        CHECK(d->label() == "山田太郎", "mnc label = card name");
        CHECK(d->display_image().w > 0 && d->display_image().h > 0, "mnc display decoded");
    }

    // Plain image -> Image: whole file decoded; label is the file basename.
    {
        auto d = NameCardData::load(assets + "/display.jpg", opts);
        CHECK(d->state() == NameCardData::State::Ok, "img state Ok");
        CHECK(d->kind() == NameCardData::Kind::Image, "img is Image");
        CHECK(!d->is_card(), "img not card");
        CHECK(d->name().empty(), "img no name");
        CHECK(d->label() == "display.jpg", "img label = basename");
        CHECK(d->display_image().w > 0 && d->display_image().h > 0, "img decoded");
    }

    // .snc.pdf has no display image -> not loadable here.
    {
        auto d = NameCardData::load(fixtures + "/basic.snc.pdf", opts);
        CHECK(d->state() == NameCardData::State::Failed, "snc Failed");
    }

    // Missing file -> Failed (never crashes).
    {
        auto d = NameCardData::load(fixtures + "/does_not_exist.jpg", opts);
        CHECK(d->state() == NameCardData::State::Failed, "missing Failed");
    }

    if (failures) {
        printf("\n%d NameCardData CHECK(s) failed\n", failures);
        return 1;
    }
    printf("NameCardData tests passed\n");
    return 0;
}
