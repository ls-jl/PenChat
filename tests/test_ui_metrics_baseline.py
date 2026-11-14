#!/usr/bin/env python3
"""Compare stable shared-control screens against an independently built baseline."""
import argparse
import json
import pathlib
import tempfile
import time
import test_lvgl_app as t
from image_gestures import capture

t.port = 1

def run(binary, output, baseline=None):
    output.mkdir(parents=True, exist_ok=True)
    for w, h in [(960, 266), (640, 260), (568, 1210)]:
        with tempfile.TemporaryDirectory(prefix='penchat-metrics-') as folder:
            a = t.Native(binary, folder, seed_model=False,
                         screen={'width': w, 'height': h, 'direction': 0})
            try:
                for page in ('chat', 'settings', 'font_size'):
                    if page == 'settings': a.open_settings()
                    if page == 'font_size': a.setting('font_size')
                    time.sleep(.25)
                    state = a.diag()
                    value = {k: state[k] for k in ('view', 'page', 'composer_bounds', 'reading_font') if k in state}
                    if page == 'settings': value['settings'] = state['settings']
                    name = f'{w}x{h}-{page}'
                    (output/(name+'.json')).write_text(json.dumps(value, ensure_ascii=False, indent=2))
                    capture(a, output/(name+'.png'))
                    if baseline:
                        assert value == json.loads((baseline/(name+'.json')).read_text()), name
                        # Both binaries use the same deterministic stb PNG encoder.
                        assert (baseline/(name+'.png')).read_bytes() == (output/(name+'.png')).read_bytes(), name
            finally: a.close()
    print('UI_METRICS_BASELINE_GEOMETRY_AND_PIXELS_OK', flush=True)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--binary', type=pathlib.Path, default=t.ROOT/'build/host/penchat-lvgl')
    p.add_argument('--output', type=pathlib.Path, default=t.ROOT/'.work/ui-metrics/current')
    p.add_argument('--baseline', type=pathlib.Path)
    args = p.parse_args()
    run(args.binary.resolve(), args.output, args.baseline)
