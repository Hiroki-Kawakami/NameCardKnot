#!/bin/sh
# Host unit test for dokan — the pure descriptor codec + KDF only (no ESP-IDF, no
# hardware). Run from anywhere. Compiles every src/*.c together with the test
# driver. Device-only code is fenced behind ESP_PLATFORM so it compiles out on the
# host; esp_err.h comes from test/shim.
set -e

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

SRCS=$(find "$ROOT/src" -name '*.c')

for t in "$DIR"/test_*.c; do
    name=$(basename "$t" .c)
    gcc -std=c11 -Wall -Wextra -O2 \
        -I "$ROOT/inc" -I "$ROOT/src" -I "$DIR/shim" \
        "$t" $SRCS \
        -o "$OUT/$name"
    echo "# $name"
    "$OUT/$name"
done
