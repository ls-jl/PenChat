#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGET=${1:-rk3562}
python3 "$ROOT/scripts/stage-fonts.py"
source "$ROOT/scripts/native-target.sh" "$TARGET"
if [ "$TARGET" != host ]; then bash "$ROOT/scripts/prepare-native-system.sh" "$TARGET"; fi
python3 "$ROOT/scripts/fetch-lvgl-deps.py"
python3 "$ROOT/scripts/prepare-microtex.py"
python3 "$ROOT/scripts/prepare-pinyin.py"
bash "$ROOT/scripts/build-render-deps.sh" "$TARGET"
bash "$ROOT/scripts/build-http-deps.sh" "$TARGET"
ARGS=(-DPENCHAT_TARGET=rk3562)
if [ "$TARGET" != host ]; then
  ARGS=(-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR="$PENCHAT_BUILD_CPU" -DCMAKE_C_COMPILER="$PENCHAT_BUILD_CC" -DCMAKE_CXX_COMPILER="$PENCHAT_BUILD_CXX" -DPENCHAT_TARGET="$TARGET")
fi
cmake -S "$ROOT/native/lvgl-app" -B "$ROOT/build/$TARGET" -DCMAKE_BUILD_TYPE=Release "${ARGS[@]}"
if [ "$TARGET" = host ]; then
  cmake --build "$ROOT/build/$TARGET" -j"${JOBS:-6}"
else
  cmake --build "$ROOT/build/$TARGET" --target penchat-lvgl jsapi_penchat_launcher penchat-sensor-guard -j"${JOBS:-6}"
  STAGE="$ROOT/build/payload-$TARGET"
  mkdir -p "$STAGE/assets/bin" "$STAGE/libs/$PENCHAT_BUILD_LIBDIR"
  for name in penchat-lvgl penchat-sensor-guard; do
    install -m755 "$ROOT/build/$TARGET/$name" "$STAGE/assets/bin/$name"
    "$PENCHAT_BUILD_STRIP" --strip-unneeded "$STAGE/assets/bin/$name"
  done
  install -m644 "$ROOT/build/$TARGET/libjsapi_penchat_launcher.so" "$STAGE/libs/$PENCHAT_BUILD_LIBDIR/"
  "$PENCHAT_BUILD_STRIP" --strip-unneeded "$STAGE/libs/$PENCHAT_BUILD_LIBDIR/libjsapi_penchat_launcher.so"
  if [ "$TARGET" = rk3562 ]; then
    mkdir -p "$ROOT/assets/bin" "$ROOT/native/prebuilt/arm64-rk3562"
    cp "$STAGE/assets/bin/"* "$ROOT/assets/bin/"
    cp "$STAGE/libs/arm64-rk3562/"* "$ROOT/native/prebuilt/arm64-rk3562/"
  fi
  python3 "$ROOT/scripts/stage-lvgl-assets.py"
fi
