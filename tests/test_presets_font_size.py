#!/usr/bin/env python3
"""Actual preset labels retain complete lines as global text size changes."""
import json
import os
import pathlib
import sqlite3
import tempfile
import test_lvgl_app as t
from image_gestures import capture
from test_ui_theme import App

t.port = 1
BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))
OUT = pathlib.Path(os.environ.get('PENCHAT_PRESET_FONT_ARTIFACTS', t.ROOT/'.work/release-0.3.1/preset-font-size'))
OUT.mkdir(parents=True, exist_ok=True)


def fixture(path, percent, ui):
    schema = (t.ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
    presets = [{'id': identity, 'name': name, 'prompt_mode': 'custom', 'prompt': 'LOCAL FIXTURE',
                'effort': 'api_default'} for identity, name in [('default', '默认字号预设'), ('other', '中文 English Ag')]]
    with sqlite3.connect(path/'penchat.db') as db:
        db.executescript(schema)
        for key, value in [('chat_presets', presets), ('presets_enabled', True),
                           ('reading_font_percent', percent), ('ui_scale_percent', ui)]:
            db.execute('insert into app_settings values(?,?,1)', (key, json.dumps(value)))


def check(d, kind, percent, ui):
    state = d['presets_layout']
    assert state['kind'] == kind and len(state['labels']) == (5 if kind == 'menu' else 4), state
    rows = {}
    for label in state['labels']:
        b = label['bounds']
        assert b[3]-b[1]+1 >= label['line_height'], label
        if label['role'] == 'heading':
            continue
        rows.setdefault(label['index'], {})[label['role']] = label
        if kind == 'page' or label['role'] == 'title':
            parent = label['row_bounds']
            assert parent[1] <= b[1] <= b[3] <= parent[3], label
    previous = None
    for row in rows.values():
        title, detail = row['title'], row['detail']
        assert title['bounds'][3] < detail['bounds'][1], row
        if previous is not None:
            assert previous < title['row_bounds'][1], rows
        previous = detail['bounds'][3]
    # Preserve established default-size menu/list geometry exactly.
    if percent == 100 and ui == 100:
        first = rows[0]
        if kind == 'menu':
            assert first['title']['row_bounds'][3]-first['title']['row_bounds'][1]+1 == 44
            assert rows[1]['title']['row_bounds'][1]-first['title']['row_bounds'][1] == 72
            assert first['detail']['bounds'][3]-first['detail']['bounds'][1]+1 == 22
        else:
            assert first['title']['row_bounds'][3]-first['title']['row_bounds'][1]+1 == 64
            assert first['detail']['bounds'][1]-first['title']['row_bounds'][1] == 36
            assert first['detail']['bounds'][3]-first['detail']['bounds'][1]+1 == 21
    return state


def run(width, height, percent, ui=100):
    name = f'{width}x{height}-text{percent}-ui{ui}'
    with tempfile.TemporaryDirectory(prefix='penchat-preset-font-') as folder:
        path = pathlib.Path(folder)
        fixture(path, percent, ui)
        app = App(path, width, height, BINARY)
        try:
            app.attachment_action('presets')
            t.wait_for(lambda: not app.diag().get('surface_motion', False))
            d = app.diag()
            check(d, 'menu', percent, ui)
            capture(app, OUT/f'{name}-menu.png')
            (OUT/f'{name}-menu.json').write_text(json.dumps(d['presets_layout'], indent=2))
            app.tap(width-5, height-5)
            app.open_settings()
            app.setting('presets')
            d = app.diag()
            check(d, 'page', percent, ui)
            capture(app, OUT/f'{name}-page.png')
            (OUT/f'{name}-page.json').write_text(json.dumps(d['presets_layout'], indent=2))
        finally:
            app.close()
    print(f'Preset typography {name}: complete lines, separated rows and retained default geometry passed.', flush=True)


if __name__ == '__main__':
    for width, height in ((960, 266), (640, 260), (568, 1210)):
        for percent in (75, 100, 150):
            run(width, height, percent)
    run(568, 1210, 150, 125)
