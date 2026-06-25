#!/bin/sh
# Host unit test for image_processor — the pure decode/grayscale/dither
# pipeline only: no ESP-IDF, no hardware, no LVGL. Run from anywhere.
#
# Compiles every src/*.cpp together with the test driver. Device-only code in
# src/ must stay fenced behind ESP_PLATFORM so it compiles out on the host.
set -e

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

SRCS=$(find "$ROOT/src" -name '*.cpp')

g++ -std=c++17 -Wall -Wextra -O2 -DIMGPROC_PROFILE=0 \
    -I "$ROOT/inc" -I "$ROOT/src" \
    "$DIR/image_processor_test.cpp" $SRCS \
    -o "$OUT/imgproc_test"
"$OUT/imgproc_test"
