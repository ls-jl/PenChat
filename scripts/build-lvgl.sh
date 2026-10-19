#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGET=${1:-orange}
python3 "$ROOT/scripts/stage-fonts.py"
bash "$ROOT/scripts/prepare-native-system.sh"
python3 "$ROOT/scripts/fetch-lvgl-deps.py"
python3 "$ROOT/scripts/prepare-microtex.py"
bash "$ROOT/scripts/build-render-deps.sh" "$TARGET"
ARGS=()
if [ "$TARGET" = orange ]; then
  SDK=${RK_SDK_ROOT:-/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK}
  GCC=${JSAPI_TOOLCHAIN_ROOT:-$SDK/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu}
  ARGS=(-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER="$GCC/bin/aarch64-linux-gnu-gcc" -DCMAKE_CXX_COMPILER="$GCC/bin/aarch64-linux-gnu-g++")
fi
cmake -S "$ROOT/native/lvgl-app" -B "$ROOT/build/$TARGET" -DCMAKE_BUILD_TYPE=Release "${ARGS[@]}"
cmake --build "$ROOT/build/$TARGET" -j"${JOBS:-6}"
if [ "$TARGET" = orange ]; then
  mkdir -p "$ROOT/assets/bin" "$ROOT/native/prebuilt/arm64-orange"
  install -m 0755 "$ROOT/build/orange/penchat-lvgl" "$ROOT/assets/bin/penchat-lvgl"
  "$GCC/bin/aarch64-linux-gnu-strip" --strip-unneeded "$ROOT/assets/bin/penchat-lvgl"
  install -m 0644 "$ROOT/build/orange/libjsapi_penchat_launcher.so" "$ROOT/native/prebuilt/arm64-orange/libjsapi_penchat_launcher.so"
  "$GCC/bin/aarch64-linux-gnu-strip" --strip-unneeded "$ROOT/native/prebuilt/arm64-orange/libjsapi_penchat_launcher.so"
  install -m 0755 "$ROOT/build/orange/penchat-sensor-guard" "$ROOT/assets/bin/penchat-sensor-guard"
  "$GCC/bin/aarch64-linux-gnu-strip" --strip-unneeded "$ROOT/assets/bin/penchat-sensor-guard"
  python3 "$ROOT/scripts/stage-lvgl-assets.py"
fi
