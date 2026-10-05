#!/usr/bin/env bash
set -euo pipefail
# Helper for this packaging workspace: application sources are the exported snapshot.
DELIVERY=$(cd -- "$(dirname -- "$0")/.." && pwd)
ORIGINAL=${ORIGINAL:?Set ORIGINAL to the development workspace with built static dependencies}
SRC="$DELIVERY/source"
OUT="$DELIVERY/runtime/app"
ZC=${ZENOHC_ROOT:?Set ZENOHC_ROOT to the extracted ARM64 zenoh-c include/lib root}
mkdir -p "$OUT"
for name in xt_camera xt_radar; do
 aarch64-linux-gnu-g++ -std=c++17 -O2 -DZENOH_LINUX -static \
  -I"$SRC/vendor/zenoh-pico/include" -I"$ORIGINAL/build/zenoh-aarch64/include" \
  -I"$SRC/vendor/json/include" -I"$ORIGINAL/vendor/curl/curl-8.21.0/include" \
  "$SRC/board/src/$name.cpp" -o "$OUT/$name" \
  "$ORIGINAL/build/zenoh-aarch64/lib/libzenohpico.a" "$ORIGINAL/build/curl/lib/libcurl.a" -lpthread -ldl -lm
done
aarch64-linux-gnu-g++ -std=c++17 -O2 -DZENOH_LINUX -DXT_USE_ZENOHC -static \
 -I"$ZC/include" -I"$SRC/vendor/json/include" -I"$ORIGINAL/vendor/curl/curl-8.21.0/include" \
 "$SRC/board/src/xt_radar.cpp" -o "$OUT/xt_gateway" \
 "$ZC/lib/libzenohc.a" "$ORIGINAL/build/curl/lib/libcurl.a" -lpthread -ldl -lm
aarch64-linux-gnu-gcc -std=c11 -O2 -static \
 -I"$ORIGINAL/vendor/curl/curl-8.21.0/include" "$SRC/vizd/vizd5.c" -o "$OUT/vizd" \
 "$ORIGINAL/build/curl/lib/libcurl.a" -lpthread -ldl -lm
file "$OUT/xt_camera" "$OUT/xt_radar" "$OUT/xt_gateway" "$OUT/vizd"
sha256sum "$OUT/xt_camera" "$OUT/xt_radar" "$OUT/xt_gateway" "$OUT/vizd"
