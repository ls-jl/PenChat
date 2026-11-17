#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/test_landscape_composer_limit.py
python3 tests/test_landscape_draft_anchor.py
python3 tests/test_landscape_input_style.py
python3 tests/test_ocr_mode_memory.py
node --test tests/js/*.test.cjs
ctest --test-dir build/host -R 'penchat-(drm-flip|drm-surface|display-recovery|runtime-lease)$' --output-on-failure
python3 tests/test_window_focus.py
python3 tests/test_runtime_residency.py
