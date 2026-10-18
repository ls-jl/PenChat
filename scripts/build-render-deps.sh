#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGET=${1:-host}
PREFIX="$ROOT/.deps/$TARGET-prefix"
BUILD="$ROOT/build/deps-$TARGET"
mkdir -p "$BUILD" "$PREFIX"
JOBS=${JOBS:-6}
ARGS=()
MESON_ARGS=()
if [ "$TARGET" = orange ]; then
  SDK=${RK_SDK_ROOT:-/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK}
  GCC=${JSAPI_TOOLCHAIN_ROOT:-$SDK/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu}
  CC="$GCC/bin/aarch64-linux-gnu-gcc"
  CXX="$GCC/bin/aarch64-linux-gnu-g++"
  cat > "$BUILD/cross.ini" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$GCC/bin/aarch64-linux-gnu-ar'
strip = '$GCC/bin/aarch64-linux-gnu-strip'
pkg-config = 'pkg-config'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = ['-fPIC']
cpp_args = ['-fPIC']
EOF
  ARGS=(-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX")
  MESON_ARGS=(--cross-file "$BUILD/cross.ini")
fi
if [ ! -f "$PREFIX/lib/libfreetype.a" ]; then
  cmake -S "$ROOT/.deps/freetype" -B "$BUILD/freetype" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON "${ARGS[@]}"
  cmake --build "$BUILD/freetype" -j"$JOBS"
  cmake --install "$BUILD/freetype"
fi
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
if [ ! -f "$PREFIX/lib/libpixman-1.a" ]; then
  meson setup "$BUILD/pixman" "$ROOT/.deps/pixman" --prefix "$PREFIX" --libdir lib \
    --default-library static -Dbuildtype=release -Dtests=disabled -Ddemos=disabled \
    -Dlibpng=disabled -Dgtk=disabled "${MESON_ARGS[@]}"
  meson compile -C "$BUILD/pixman" -j"$JOBS"
  meson install -C "$BUILD/pixman"
fi
if [ ! -f "$PREFIX/lib/libcairo.a" ]; then
  meson setup "$BUILD/cairo" "$ROOT/.deps/cairo" --prefix "$PREFIX" --libdir lib \
    --default-library static -Dbuildtype=release -Dfontconfig=disabled -Dfreetype=enabled \
    -Dpng=disabled -Dzlib=disabled -Dxlib=disabled -Dxcb=disabled -Dquartz=disabled \
    -Dglib=disabled -Dtests=disabled -Dtee=disabled -Dlzo=disabled -Dspectre=disabled \
    -Dsymbol-lookup=disabled "${MESON_ARGS[@]}"
  meson compile -C "$BUILD/cairo" -j"$JOBS"
  meson install -C "$BUILD/cairo"
fi
