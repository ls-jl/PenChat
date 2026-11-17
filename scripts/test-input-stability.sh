#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/test_landscape_composer_limit.py
python3 tests/test_landscape_draft_anchor.py
python3 tests/test_landscape_input_style.py
