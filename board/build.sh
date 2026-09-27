#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CROSS=${CROSS-aarch64-linux-gnu-}
command -v "${CROSS}g++" >/dev/null
command -v cmake >/dev/null
BUILD=${BUILD:-"$ROOT/build"}
JOBS=${JOBS:-2}
ZSOURCE=${ZSOURCE:-"$ROOT/vendor/zenoh-pico"}
ZBUILD=${ZBUILD:-"$BUILD/zenoh-aarch64"}
if [ "${REUSE_ZENOH:-0}" != 1 ]; then
cmake -S "$ZSOURCE" -B "$ZBUILD" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DFRAG_MAX_SIZE=300000 \
  -DZ_FEATURE_UNSTABLE_API=1 \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER="${CROSS}gcc"
cmake --build "$ZBUILD" --parallel "$JOBS"
fi
if ! grep -Eq '^#define Z_FEATURE_UNSTABLE_API([[:space:]]+1)?[[:space:]]*$' "$ZBUILD/include/zenoh-pico/config.h"; then
  echo 'Zenoh must be rebuilt with Z_FEATURE_UNSTABLE_API=1 for explicit reliability QoS.' >&2
  exit 1
fi
if ! grep -q '^#define Z_FRAG_MAX_SIZE 300000$' "$ZBUILD/include/zenoh-pico/config.h"; then
  echo 'Zenoh must be rebuilt with FRAG_MAX_SIZE=300000; refusing incompatible cache.' >&2
  exit 1
fi
CURL_SOURCE="$ROOT/vendor/curl/curl-8.21.0"
if [ ! -f "$CURL_SOURCE/CMakeLists.txt" ]; then
  tar -xf "$ROOT/vendor/curl/curl-8.21.0.tar.xz" -C "$ROOT/vendor/curl"
fi
if [ "${REUSE_CURL:-0}" != 1 ]; then
cmake -S "$CURL_SOURCE" -B "$BUILD/curl" -DCMAKE_C_COMPILER="${CROSS}gcc" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
  -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DHTTP_ONLY=ON -DCURL_ENABLE_SSL=OFF \
  -DCURL_USE_LIBPSL=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
  -DUSE_NGHTTP2=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_LIBIDN2=OFF \
  -DENABLE_THREADED_RESOLVER=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF
cmake --build "$BUILD/curl" --parallel "$JOBS"
fi
test -f "$BUILD/curl/lib/libcurl.a"
mkdir -p "$BUILD/board"
for name in xt_camera xt_radar; do
  source=$name
  "${CROSS}g++" -std=c++17 -O2 -DZENOH_LINUX -static \
    -I"$ZSOURCE/include" -I"$ZBUILD/include" -I"$ROOT/vendor/json/include" \
    -I"$CURL_SOURCE/include" "$ROOT/board/src/$source.cpp" -o "$BUILD/board/$name" \
    -L"$ZBUILD/lib" -lzenohpico "$BUILD/curl/lib/libcurl.a" -lpthread -ldl -lm
done
if [ -z "${ZENOHC_ROOT:-}" ]; then
  echo 'Set ZENOHC_ROOT to the zenoh-c 1.10 ARM64 package before building xt_gateway.' >&2
  exit 1
fi
OUTPUT="$BUILD/board/xt_gateway" ZENOHC_ROOT="$ZENOHC_ROOT" bash "$ROOT/board/build_zenohc_gateway.sh"
sha256sum "$BUILD/board/xt_camera" "$BUILD/board/xt_radar" "$BUILD/board/xt_gateway"
