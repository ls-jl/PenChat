#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source "$ROOT/scripts/native-target.sh" "${1:?target}"
cmake -S "$ROOT/native/lvgl-app" -B "$ROOT/build/$PENCHAT_BUILD_TARGET" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR="$PENCHAT_BUILD_CPU" \
  -DCMAKE_C_COMPILER="$PENCHAT_BUILD_CC" -DCMAKE_CXX_COMPILER="$PENCHAT_BUILD_CXX" -DPENCHAT_TARGET="$PENCHAT_BUILD_TARGET"
cmake --build "$ROOT/build/$PENCHAT_BUILD_TARGET" --target jsapi_penchat_rescue -j"${JOBS:-4}"
