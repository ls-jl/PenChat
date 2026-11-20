#!/usr/bin/env python3
"""Real LVGL helper tests; temporary data and test-only scale injection."""
import pathlib
import os
import json
import subprocess
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
BUILD=pathlib.Path(os.environ.get('PENCHAT_TEST_BUILD_DIR',ROOT/'build/host'))
for scale in (75, 100, 125, 150):
    for width, height in ((960, 266), (640, 260), (568, 1210)):
        with tempfile.TemporaryDirectory(prefix='penchat-controls-') as folder:
            # MicroTeX is process-scoped, as in the real application.
            subprocess.run([str(BUILD/'penchat-ui-controls-tests'), str(ROOT), folder,
                            str(scale), str(width), str(height)], check=True)
    with tempfile.TemporaryDirectory(prefix='penchat-ui-scale-cli-') as folder:
        folder = pathlib.Path(folder)
        config = folder/'screen.json'
        config.write_text(json.dumps({'screen': {'width': 960, 'height': 266, 'direction': 0}}))
        subprocess.run([str(BINARY), '--headless', '--ui-scale-percent', str(scale),
                        '--data-dir', str(folder), '--screen-config', str(config), '--workspace', str(ROOT),
                        '--font', str(ROOT/'.deps/chinese.ttf'), '--tex-resources', str(ROOT/'.deps/microtex/res'),
                        '--frames', '1'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
# Invalid/production-shaped invocations must fail before opening a display or DB.
for options in (['--ui-scale-percent', '125'],
                ['--headless', '--ui-scale-percent', '74'],
                ['--headless', '--ui-scale-percent', '151'],
                ['--headless', '--ui-scale-percent', '125junk'],
                ['--headless', '--ui-scale-percent', '100.0']):
    result = subprocess.run([str(BINARY)] + options, capture_output=True)
    assert result.returncode != 0
    assert b'UI scal' in result.stderr, result.stderr
print('UI_CONTROL_SCALE_MATRIX_AND_HOST_GUARD_OK')
