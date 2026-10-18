#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
python3 "$ROOT/scripts/stage-fonts.py"
python3 "$ROOT/scripts/prepare-scanner.py" --stage
[ -x "$ROOT/assets/bin/penchat-scanner" ] || { echo 'Build scanner native executable first.' >&2; exit 1; }
"$ROOT/scripts/stage-native-libs.sh"
rm -rf .falcon_ .falcon_tmp
aiot-cli -c -q -p
mkdir -p dist
PACKAGE=$(node -e 'const p=require("./package.json");process.stdout.write(`${p.appid}.${p.version.replace(/\./g,"_")}.amr`)')
cp "$PACKAGE" "dist/$PACKAGE"
