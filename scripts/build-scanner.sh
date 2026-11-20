#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd)"
task_target="${1:-host}"
case "$task_target" in host|rk3562|cv1826-s) ;; *) echo "Unknown native target: $task_target" >&2; exit 2;; esac
if [ "$task_target" = cv1826-s ]; then exec bash "$task_root/scripts/build-scanner-cv1826-s.sh"; fi
task_bundle="${PENCHAT_SCAN_BUNDLE:-/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018}"
python3 "$task_root/scripts/prepare-scanner.py" --bundle "$task_bundle"
if [ "$task_target" = host ]; then
  export LD_LIBRARY_PATH="$task_bundle/third_party/opencv-host/lib/x86_64-linux-gnu:$task_bundle/third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 OPENCV_OPENCL_RUNTIME=disabled
  cmake -S "$task_root/native/scanner" -B "$task_root/build/scanner-host" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSCANNER_TARGET=rk3562 -DSCANNER_CV_ROOT="$task_bundle/third_party/opencv-host"
  cmake --build "$task_root/build/scanner-host" -j4
else
  python3 "$task_root/scripts/prepare-scanner.py" --bundle "$task_bundle" --cross
  PENCHAT_SCAN_TOOLCHAIN_FILE="${PENCHAT_SCAN_TOOLCHAIN_FILE:-$task_root/native/scanner/rk3562-toolchain.cmake}"
  cmake -S "$task_root/native/scanner" -B "$task_root/build/scanner-rk3562" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PENCHAT_SCAN_TOOLCHAIN_FILE" \
    -DSCANNER_TARGET=rk3562 -DSCANNER_TESTS=OFF -DSCANNER_BUILD_REPLAY="${PENCHAT_SCAN_REPLAY:-OFF}" -DSCANNER_CV_ROOT="$task_bundle/third_party/opencv-arm" \
    -DSCANNER_PNG_ROOT="$task_root/.deps/scanner-png"
  cmake --build "$task_root/build/scanner-rk3562" -j4
  python3 "$task_root/scripts/prepare-scanner.py" --bundle "$task_bundle" --stage
  mkdir -p "$task_root/assets/bin"
  install -m755 "$task_root/build/scanner-rk3562/penchat-scanner" "$task_root/assets/bin/penchat-scanner"
  bash "$task_root/scripts/strip-production.sh" "$task_root/assets/bin/penchat-scanner"
fi
