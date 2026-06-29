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
IMGF=$(CDPATH= cd -- "$COMPONENTS/../esp-devkit/libs/image_framework" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# image_processor is pure orchestration over image_framework — compile the imgf
# C sources (gcc, C11) and link them with the C++ orchestration + test.
for c in "$IMGF"/src/*.c; do
    gcc -std=c11 -Wall -Wextra -O2 -I "$IMGF/inc" -I "$IMGF/src" -c "$c" \
        -o "$OUT/$(basename "${c%.c}").o"
done

SRCS="$(find "$ROOT/src" -name '*.cpp') $(find "$IMG/src" -name '*.cpp')"

g++ -std=c++17 -Wall -Wextra -O2 -DIMGPROC_PROFILE=0 -DIMGPROC_ASYNC=0 \
    -I "$ROOT/inc" -I "$IMG/inc" -I "$IMGF/inc" \
    "$DIR/e2e.cpp" $SRCS "$OUT"/*.o \
    -lm -lpthread \
    -o "$OUT/nckpdf_e2e"
"$OUT/nckpdf_e2e" "$DIR/fixtures"
