#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/.deps/include" "$ROOT/.deps/orange-libs"
if [ ! -f "$ROOT/.deps/include/sqlite3.h" ]; then
  cp "${SQLITE_INCLUDE_DIR:-/usr/include}/sqlite3.h" "$ROOT/.deps/include/"
fi
SERIAL=${PENCHAT_ADB_SERIAL:-MEB0400008900794}
for name in libsqlite3.so.0; do
  if [ ! -f "$ROOT/.deps/orange-libs/$name" ]; then
    adb -s "$SERIAL" shell 'cat /proc/device-tree/model' | tr '\0' '\n' | grep -q ORANGE
    adb -s "$SERIAL" pull "/usr/lib/$name" "$ROOT/.deps/orange-libs/$name"
  fi
done
if [ ! -f "$ROOT/.deps/chinese.ttf" ]; then
  if [ -n "${PENCHAT_TEST_FONT:-}" ]; then
    cp "$PENCHAT_TEST_FONT" "$ROOT/.deps/chinese.ttf"
  else
    adb -s "$SERIAL" pull /etc/miniapp/resources/fonts/HarmonyOS_Sans_SC_Regular.ttf "$ROOT/.deps/chinese.ttf"
  fi
fi
