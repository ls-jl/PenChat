#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/native/camera/build-host/image-data-test"
mkdir -p "$(dirname "$OUT")"
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -I"$ROOT/native/camera/src" \
  "$ROOT/native/camera/tests/ImageDataTest.cpp" "$ROOT/native/camera/src/image_data.cpp" -o "$OUT"
"$OUT"
