#!/usr/bin/env python3
"""Merged portrait menus retain live input and resolve their new design units."""
import json
import os
import pathlib
import sqlite3
import tempfile

import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture

t.port = 1
OUT = t.ROOT / '.work/dpi-merge/popups'
OUT.mkdir(parents=True, exist_ok=True)
os.environ['PENCHAT_TEST_Y07_ROUNDED'] = '1'
try:
    for scale in (75, 100, 125, 150):
        dp = lambda n: (n * scale + 50) // 100
        with tempfile.TemporaryDirectory(prefix='penchat-merged-popups-') as folder:
            folder = pathlib.Path(folder)
            seed(folder, 1)
            presets = [dict(id='default' if i == 0 else 'p'+str(i), name='预设 '+str(i),
                            prompt_mode='inherit', model_id='', effort='') for i in range(3)]
            with sqlite3.connect(folder/'penchat.db') as db:
                for key, value in [('chat_presets', presets), ('presets_enabled', True)]:
                    db.execute('insert into app_settings values(?,?,1)', (key, json.dumps(value)))
            a = t.Native(t.ROOT/'build/host/penchat-lvgl', folder, seed_model=False,
                         official_adapter=False, screen={'width': 568, 'height': 1210},
                         ui_scale_percent=scale)
            try:
                def tap(bounds):
                    a.tap((bounds[0]+bounds[2])//2, (bounds[1]+bounds[3])//2)

                def open_menu(name):
                    tap(a.diag()['attachment_button'])
                    t.wait_for(lambda: a.diag()['popup'] == 'attachments')
                    a.attachment_action(name)
                    t.wait_for(lambda: a.diag()['popup'] == name)
                    bounds = a.diag()['popup_bounds']
                    assert 0 <= bounds[0] < bounds[2] < 568
                    assert 0 <= bounds[1] < bounds[3] < 1210
                    return bounds

                # Exact natural heights catch raw constants surviving a merge.
                for name, height in [('models', dp(8)+dp(30)+2*dp(46)),
                                     ('presets', dp(32)+3*dp(72)+dp(4))]:
                    bounds = open_menu(name)
                    if name == 'presets':
                        # Rounded component heights can exceed a rounded 72dp
                        # pitch. Keep exact natural sizing, using real font lines.
                        labels = a.diag()['presets_layout']['labels']
                        heading = next(x for x in labels if x['role']=='heading')
                        titles = [x for x in labels if x['role']=='title']
                        details = [x for x in labels if x['role']=='detail']
                        assert len(titles)==len(details)==3
                        assert all(x['bounds'][3]-x['bounds'][1]+1>=x['line_height'] for x in labels)
                        first = max(dp(32),2*dp(4)+max(dp(24),heading['line_height']))
                        button = max(dp(44),max(x['line_height'] for x in titles)+dp(8))
                        detail = max(dp(22),max(x['line_height'] for x in details))
                        height = first+3*max(dp(72),button+detail+dp(6))+dp(4)
                    assert bounds[3]-bounds[1]+1 == min(dp(360), height), (scale, name, bounds)
                    assert bounds[2]-bounds[0]+1 <= dp(440)
                    capture(a, OUT/f'{scale}-{name}.png')
                    a.tap(566, 400)
                    t.wait_for(lambda: not a.diag()['attachments'])

                # A popup may cover the original field; it must retain its
                # keyboard, pending pinyin and OCR registration without rebuilding.
                tap(a.diag()['composer_text_bounds'])
                t.wait_for(lambda: a.diag()['editor']['active'] and a.diag()['editor']['keys'])
                for value in ('n', 'i'):
                    key = next(k for k in a.diag()['editor']['keys'] if k['key'] == value)
                    a.tap(key['x'], key['y'])
                t.wait_for(lambda: not a.diag()['editor']['pending'])
                before = a.diag()['editor']
                for name, y in [('models', dp(4)+dp(30)+dp(22)),
                                ('presets', dp(32)+dp(22))]:
                    bounds = open_menu(name)
                    a.tap(bounds[0]+dp(100), bounds[1]+y)
                    t.wait_for(lambda: not a.diag()['attachments'])
                    after = a.diag()['editor']
                    for field in ('session', 'preedit_length', 'ocr_request', 'build_count', 'keys'):
                        assert after[field] == before[field], (scale, name, field)
                assert a.diag()['active_model'] == 'a'
                assert a.diag()['session_preferences']['preset_id'] == 'default'
                assert not a.diag()['generating']
                assert not a.read_messages()
                print('MERGED_PORTRAIT_POPUP_UNITS_AND_RETAINED_INPUT', scale, flush=True)
            finally:
                a.close()
finally:
    os.environ.pop('PENCHAT_TEST_Y07_ROUNDED', None)
