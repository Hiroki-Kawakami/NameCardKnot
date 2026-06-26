#!/bin/sh
# Host unit test for NameCardData (no ESP-IDF, no LVGL, no SDL): the file-type
# abstraction over plain images and .mnc.pdf, decoding through image_processor.
# Run `npm run gen-fixtures` in editor/ first (it produces the .mnc.pdf golden +
# assets this test loads). FreeRTOS-free: decode_file_async runs synchronously
# with IMGPROC_ASYNC=0.
set -e

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP=$(CDPATH= cd -- "$DIR/.." && pwd)
ROOT=$(CDPATH= cd -- "$APP/.." && pwd)
NCK="$ROOT/components/namecard_pdf"
IMG="$ROOT/components/image_processor"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

SRCS="$APP/NameCardData.cpp $(find "$NCK/src" -name '*.cpp') $(find "$IMG/src" -name '*.cpp')"

g++ -std=c++17 -Wall -Wextra -O2 -DIMGPROC_ASYNC=0 -DIMGPROC_PROFILE=0 \
    -I "$APP" -I "$NCK/inc" -I "$IMG/inc" \
    "$DIR/namecard_data_test.cpp" $SRCS \
    -o "$OUT/namecard_data_test"
"$OUT/namecard_data_test" "$NCK/test/fixtures" "$NCK/test/assets"
