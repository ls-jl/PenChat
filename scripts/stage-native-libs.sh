#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/native/prebuilt/arm64-rk3562/libjsapi_penchat_launcher.so"
[ -f "$SOURCE" ] || { echo 'Build the LVGL native targets first: npm run build:native' >&2; exit 1; }
[ -x "$ROOT/assets/bin/penchat-lvgl" ] || { echo 'Missing LVGL executable' >&2; exit 1; }
[ -d "$ROOT/assets/microtex/fonts" ] || { echo 'Missing math resources' >&2; exit 1; }
python3 "$ROOT/scripts/stage-rescue.py" rk3562 "$ROOT"
