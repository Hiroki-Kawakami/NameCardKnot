#!/bin/sh
# End-to-end host test: namecard_pdf (extract embedded JPEG) -> image_processor
# (decode) — the real device path, no ESP-IDF/hardware. Run `npm run
# gen-fixtures` in editor/ first. image_processor device-only code is fenced
# behind ESP_PLATFORM / IMGPROC_* (set to 0 here, same as its own run.sh).
set -e

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/.." && pwd)
COMPONENTS=$(CDPATH= cd -- "$ROOT/.." && pwd)
IMG="$COMPONENTS/image_processor"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

SRCS="$(find "$ROOT/src" -name '*.cpp') $(find "$IMG/src" -name '*.cpp')"

g++ -std=c++17 -Wall -Wextra -O2 -DIMGPROC_PROFILE=0 -DIMGPROC_ASYNC=0 \
    -I "$ROOT/inc" -I "$IMG/inc" \
    "$DIR/e2e.cpp" $SRCS \
    -o "$OUT/nckpdf_e2e"
"$OUT/nckpdf_e2e" "$DIR/fixtures"
