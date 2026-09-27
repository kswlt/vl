#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${BUILD:-/tmp/xtbuild}
ZSOURCE=${ZSOURCE:-$ROOT/vendor/zenoh-pico}
ZBUILD=${ZBUILD:-$BUILD/zenoh-aarch64}
FLAGS=(-std=c++17 -O0 -g1 -DZENOH_LINUX -fsanitize=address,undefined -fno-omit-frame-pointer
  -I"$ZSOURCE/include" -I"$ZBUILD/include" -I"$ROOT/vendor/json/include"
  -I"$ROOT/vendor/curl/curl-8.21.0/include")
for name in board_unit snapshot_unit h264_unit; do
  g++ "${FLAGS[@]}" "$ROOT/tests/$name.cpp" -o "$BUILD/$name" \
    "$ZBUILD/lib/libzenohpico.a" "$BUILD/curl/lib/libcurl.a" -lpthread -ldl -lm
  ASAN_OPTIONS=detect_leaks=1 "$BUILD/$name"
done
g++ -std=c++17 -O2 -DZENOH_LINUX \
  -I"$ZSOURCE/include" -I"$ZBUILD/include" "$ROOT/tests/qos_probe.cpp" \
  -o "$BUILD/qos_probe" "$ZBUILD/lib/libzenohpico.a" -lpthread -ldl -lm
