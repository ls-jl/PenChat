#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGET=${1:-host}
PREFIX="$ROOT/.deps/$TARGET-prefix"
BUILD="$ROOT/build/http-$TARGET-8.17.0"
ARGS=()
if [ "$TARGET" = orange ]; then
  SDK=${RK_SDK_ROOT:-/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK}
  GCC=${JSAPI_TOOLCHAIN_ROOT:-$SDK/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu}
  SYS="$ROOT/.deps/http-system"
  mkdir -p "$SYS/include" "$SYS/lib"
  for lib in libssl.so.1.1 libcrypto.so.1.1 libz.so.1; do
    [ -f "$SYS/lib/$lib" ] || adb -s "${PENCHAT_ADB_SERIAL:-MEB0400008900794}" pull "/usr/lib/$lib" "$SYS/lib/$lib"
  done
  cp /usr/include/zlib.h /usr/include/zconf.h "$SYS/include/"
  if [ ! -f "$ROOT/.deps/openssl-headers/include/openssl/opensslconf.h" ]; then
    (cd "$ROOT/.deps/openssl-headers"; perl Configure linux-aarch64 no-shared no-tests; make build_generated)
  fi
  ARGS=(-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64
    -DCMAKE_C_COMPILER="$GCC/bin/aarch64-linux-gnu-gcc"
    -DOPENSSL_INCLUDE_DIR="$ROOT/.deps/openssl-headers/include"
    -DOPENSSL_SSL_LIBRARY="$SYS/lib/libssl.so.1.1" -DOPENSSL_CRYPTO_LIBRARY="$SYS/lib/libcrypto.so.1.1"
    -DZLIB_INCLUDE_DIR="$SYS/include" -DZLIB_LIBRARY="$SYS/lib/libz.so.1")
fi
cmake -S "$ROOT/.deps/curl" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF \
  -DCURL_USE_OPENSSL=ON -DCURL_USE_PKGCONFIG=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \
  -DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DUSE_NGTCP2=OFF -DUSE_QUICHE=OFF \
  -DCURL_ZLIB=ON -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DENABLE_THREADED_RESOLVER=ON \
  -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DCURL_DISABLE_FTP=ON -DCURL_DISABLE_FILE=ON \
  -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_TELNET=ON -DCURL_DISABLE_TFTP=ON -DCURL_DISABLE_POP3=ON \
  -DCURL_DISABLE_IMAP=ON -DCURL_DISABLE_SMTP=ON -DCURL_DISABLE_RTSP=ON -DCURL_DISABLE_GOPHER=ON \
  -DCURL_DISABLE_MQTT=ON -DCURL_DISABLE_SMB=ON -DCURL_DISABLE_WEBSOCKETS=ON \
  -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt -DCURL_CA_PATH=/etc/ssl/certs "${ARGS[@]}"
cmake --build "$BUILD" --target libcurl_static -j"${JOBS:-6}"
mkdir -p "$PREFIX/lib"
install -m 0644 "$BUILD/lib/libcurl.a" "$PREFIX/lib/libcurl.a"
