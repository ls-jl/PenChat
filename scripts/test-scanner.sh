#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$task_root"
task_bundle="${PENCHAT_SCAN_BUNDLE:-/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018}"
export LD_LIBRARY_PATH="$task_bundle/third_party/opencv-host/lib/x86_64-linux-gnu:$task_bundle/third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
ctest --test-dir build/scanner-host --output-on-failure
python3 tests/test_scanner_process.py
python3 tests/test_scanner_ui.py
