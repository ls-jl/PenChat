#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/native/prebuilt/arm64-orange/libjsapi_penchat_launcher.so"
[ -f "$SOURCE" ] || { echo 'Build the LVGL native targets first: npm run build:native' >&2; exit 1; }
[ -x "$ROOT/assets/bin/penchat-lvgl" ] || { echo 'Missing LVGL executable' >&2; exit 1; }
[ -d "$ROOT/assets/microtex/fonts" ] || { echo 'Missing math resources' >&2; exit 1; }
rm -rf "$ROOT/libs"
mkdir -p "$ROOT/libs/arm64-orange"
LOADER_ID=$(python3 -c 'import hashlib,sys;print(int(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest()[:12],16))' "$SOURCE")
cp "$SOURCE" "$ROOT/libs/libjsapi_penchat_launcher_$LOADER_ID.so"
cp "$SOURCE" "$ROOT/libs/arm64-orange/libjsapi_penchat_launcher.so"
