#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source "$ROOT/scripts/native-target.sh" cv1826-s
python3 "$ROOT/scripts/prepare-scanner-cvi.py"
python3 "$ROOT/scripts/prepare-cv1826-s-deps.py"
CVI_SOURCE="$ROOT/.deps/opencv-cv1826-s-source"
cmake -S "$CVI_SOURCE" -B "$ROOT/build/opencv-cv1826-s" -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
  -DCMAKE_C_COMPILER="$PENCHAT_BUILD_CC" -DCMAKE_CXX_COMPILER="$PENCHAT_BUILD_CXX" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ROOT/.deps/opencv-cv1826-s" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_LIST=core,imgproc -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
  -DBUILD_opencv_apps=OFF -DWITH_IPP=OFF -DWITH_ITT=OFF -DWITH_OPENCL=OFF -DWITH_TBB=OFF -DWITH_OPENMP=OFF \
  -DWITH_FFMPEG=OFF -DWITH_V4L=OFF -DWITH_JPEG=OFF -DWITH_PNG=OFF -DWITH_WEBP=OFF -DWITH_PROTOBUF=OFF \
  -DBUILD_ZLIB=ON -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF -DENABLE_NEON=OFF -DCPU_BASELINE= -DCPU_DISPATCH=
cmake --build "$ROOT/build/opencv-cv1826-s" -j"${JOBS:-6}"
cmake --install "$ROOT/build/opencv-cv1826-s"
cmake -S "$ROOT/native/scanner" -B "$ROOT/build/scanner-cv1826-s" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm -DCMAKE_C_COMPILER="$PENCHAT_BUILD_CC" \
  -DCMAKE_CXX_COMPILER="${PENCHAT_CV1826_CLANG:-/usr/lib/llvm-18/bin/clang++}" -DCMAKE_CXX_COMPILER_TARGET=arm-linux-gnueabihf \
  -DCMAKE_CXX_FLAGS="--gcc-toolchain=$PENCHAT_BUILD_GCC --sysroot=$PENCHAT_BUILD_GCC/arm-linux-gnueabihf/libc -march=armv7-a -mfpu=neon -mfloat-abi=hard" \
  -DSCANNER_TARGET=cv1826-s -DSCANNER_TESTS=OFF -DSCANNER_CV_ROOT="$ROOT/.deps/opencv-cv1826-s" -DSCANNER_PNG_ROOT="$ROOT/.deps/cv1826-s-png"
cmake --build "$ROOT/build/scanner-cv1826-s" --target penchat-scanner penchat_cvi_media -j"${JOBS:-6}"
STAGE="$ROOT/build/payload-cv1826-s/assets"
mkdir -p "$STAGE/bin" "$STAGE/scanner/lib"
python3 "$ROOT/scripts/prepare-scanner-cvi.py" --stage "$STAGE/scanner"
install -m755 "$ROOT/build/scanner-cv1826-s/penchat-scanner" "$STAGE/bin/penchat-scanner"
install -m644 "$ROOT/build/scanner-cv1826-s/libpenchat_cvi_media.so" "$STAGE/scanner/lib/"
cp "$ROOT/.deps/cv1826-s-png/lib/"* "$STAGE/scanner/lib/"
cp "$ROOT/native/scanner/cvi-media.lock.json" "$STAGE/scanner/"
for file in "$STAGE/bin/penchat-scanner" "$STAGE/scanner/lib/"*; do "$PENCHAT_BUILD_STRIP" --strip-unneeded "$file"; done
