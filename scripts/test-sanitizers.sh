#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$task_root"
task_bundle="${PENCHAT_SCAN_BUNDLE:-/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018}"
export LD_LIBRARY_PATH="$task_bundle/third_party/opencv-host/lib/x86_64-linux-gnu:$task_bundle/third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
cmake -S native/scanner -B build/scanner-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSCANNER_SANITIZE=ON -DSCANNER_CV_ROOT="$task_bundle/third_party/opencv-host" \
  -DCMAKE_EXE_LINKER_FLAGS=-no-pie
cmake --build build/scanner-sanitize -j4
cmake -S native/lvgl-app -B build/asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie'
cmake --build build/asan -j4
# The production engine's RSS guard remains active under the test allocator.
export ASAN_OPTIONS=detect_leaks=1:quarantine_size_mb=8:thread_local_quarantine_size_kb=64:malloc_context_size=10
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export LSAN_OPTIONS="suppressions=$task_root/tests/lsan.supp"
export PENCHAT_TEST_TIMEOUT=30
export PENCHAT_TEST_BINARY="$task_root/build/asan/penchat-lvgl"
export PENCHAT_SCANNER_TEST_BINARY="$task_root/build/scanner-sanitize/penchat-scanner"
ctest --test-dir build/scanner-sanitize --output-on-failure
ctest --test-dir build/asan --output-on-failure
python3 tests/test_scanner_process.py
python3 tests/test_scanner_ui.py
python3 tests/test_chat_style.py
