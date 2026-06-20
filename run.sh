#!/bin/sh
set -e

TARGET=${1:-simulator}

case "$TARGET" in
  simulator)
    [ -d build ] || cmake --fresh -S simulator -B build -G Ninja
    cmake --build build
    ./build/simulator
    ;;
  simverify)
    # Headless, scripted UI verification (no host window): build, then run the
    # given sim-harness script (see esp-devkit/sim_harness/sim_harness.h).
    # Captured frames go wherever the script's `capture` lines point.
    SCRIPT=$2
    if [ -z "$SCRIPT" ]; then
      echo "Usage: $0 simverify <script>   (e.g. simulator/verify/smoke.txt)"
      exit 1
    fi
    [ -d build ] || cmake --fresh -S simulator -B build -G Ninja
    cmake --build build
    SIMULATOR_HEADLESS=1 SIMULATOR_SCRIPT="$SCRIPT" ./build/simulator
    ;;
  esp32s3)
    idf.py -C esp32s3 flash monitor
    ;;
  *)
    echo "Usage: $0 [simulator|simverify <script>|esp32s3]"
    exit 1
    ;;
esac
