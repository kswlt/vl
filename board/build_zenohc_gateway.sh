#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CROSS=${CROSS-aarch64-linux-gnu-}
ZENOHC_ROOT=${ZENOHC_ROOT:?Set ZENOHC_ROOT to the extracted official zenoh-c 1.10 ARM64 package}
CURL_SOURCE=${CURL_SOURCE:-"$ROOT/vendor/curl/curl-8.21.0"}
OUTPUT=${OUTPUT:-"$ROOT/build/board/xt_gateway"}

test -f "$ZENOHC_ROOT/include/zenoh.h"
test -f "$ZENOHC_ROOT/lib/libzenohc.a"
test -f "$ROOT/build/curl/lib/libcurl.a"
mkdir -p "$(dirname "$OUTPUT")"

"${CROSS}g++" -std=c++17 -O2 -DZENOH_LINUX -DXT_USE_ZENOHC -static \
  -I"$ZENOHC_ROOT/include" -I"$ROOT/vendor/json/include" -I"$CURL_SOURCE/include" \
  "$ROOT/board/src/xt_radar.cpp" -o "$OUTPUT" \
  "$ZENOHC_ROOT/lib/libzenohc.a" "$ROOT/build/curl/lib/libcurl.a" \
  -lpthread -ldl -lm

sha256sum "$OUTPUT"
