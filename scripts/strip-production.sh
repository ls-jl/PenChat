#!/usr/bin/env bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/.." && pwd)
TASK_SDK=${RK_SDK_ROOT:-/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK}
TASK_GCC=${JSAPI_TOOLCHAIN_ROOT:-$TASK_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu}
TASK_STRIP=${PENCHAT_STRIP:-$TASK_GCC/bin/aarch64-linux-gnu-strip}
if [ "$#" -eq 0 ]; then
  set -- "$TASK_ROOT/assets/bin" "$TASK_ROOT/assets/scanner/lib" "$TASK_ROOT/native/prebuilt/arm64-orange"
fi
# Strip only staged production copies. Build outputs remain available for debugging.
python3 - "$TASK_STRIP" "$@" <<'PY'
import pathlib, subprocess, sys
for root in map(pathlib.Path, sys.argv[2:]):
    if not root.exists():
        raise SystemExit('Missing production payload: '+str(root))
    for path in sorted(root.rglob('*')) if root.is_dir() else [root]:
        if not path.is_file() or path.is_symlink():
            continue
        with path.open('rb') as source:
            header=source.read(20)
        if not header.startswith(b'\x7fELF'):
            continue
        if header[4:6]!=b'\x02\x01' or int.from_bytes(header[18:20],'little')!=183:
            raise SystemExit('Refusing non-AArch64 ELF: '+str(path))
        subprocess.run([sys.argv[1], '--strip-unneeded', str(path)], check=True)
PY
