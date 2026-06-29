#!/bin/sh
# Host unit test for image_processor — the pure decode/grayscale/dither
# pipeline only: no ESP-IDF, no hardware, no LVGL. Run from anywhere.
#
# image_processor is now pure orchestration over esp-devkit/libs/image_framework,
# so this compiles the imgf C sources (gcc, C11) into objects and links them with
# the component's C++ orchestration + the test driver (g++, C++17). Device-only
# code in src/ must stay fenced behind ESP_PLATFORM so it compiles out here.
set -e

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/.." && pwd)
IMGF=$(CDPATH= cd -- "$ROOT/../../esp-devkit/libs/image_framework" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# image_framework C sources -> objects.
for c in "$IMGF"/src/*.c; do
    obj="$OUT/$(basename "${c%.c}").o"
    gcc -std=c11 -Wall -Wextra -O2 -I "$IMGF/inc" -I "$IMGF/src" -c "$c" -o "$obj"
done

CPP_SRCS=$(find "$ROOT/src" -name '*.cpp')

# Build twice: serial (IMGPROC_ASYNC=0) and the real two-task imgf_async path
# (=1, pthreads on host) so DecodeJob's async path is exercised end-to-end.
for async in 0 1; do
    g++ -std=c++17 -Wall -Wextra -O2 -DIMGPROC_PROFILE=0 -DIMGPROC_ASYNC=$async \
        -I "$ROOT/inc" -I "$ROOT/src" -I "$IMGF/inc" \
        "$DIR/image_processor_test.cpp" $CPP_SRCS "$OUT"/*.o \
        -lm -lpthread \
        -o "$OUT/imgproc_test_$async"
    echo "--- IMGPROC_ASYNC=$async ---"
    "$OUT/imgproc_test_$async"
done
