#!/usr/bin/env python3
"""Actual theme controls, durable failure/lifecycle state and visual matrix.

Run without arguments for targeted behavioral acceptance. ``--matrix`` captures
both themes at every shipping geometry/scale using only private fixture data.
"""
import argparse
import http.server
import json
import os
import pathlib
import sqlite3
import tempfile
import threading
import time

import test_lvgl_app as t
from choice_ui import select
from image_gestures import capture
from test_font_size import fixture
from ui_scale_ui import back, open_settings, tap

BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))
OUT = t.ROOT/'.work/ui-unification/theme'
t.port = 1


class App(t.Native):
    def settings_swipe(self, delta):
        d = self.diag()
        v = d['page']['viewport'] if d.get('page') else d['chat_header']['reader_bounds']
        if d['view'] == 'chat' and not d['composer_hidden']:
            v = [v[0], v[1], v[2], min(v[3], d['composer_bounds'][1]-8)]
        safe_start = max(v[1]+35, d['safe_insets'][1]+30)
        start = min(v[3]-12, max(safe_start, v[1]+150)) if delta < 0 else safe_start
        end = max(v[1]+12, min(v[3]-12, start+delta))
        x = (v[0]+v[2])//2
        for i in range(7):
            self.send('touch', x=x, y=start+(end-start)*i//6, pressed=True)
            time.sleep(.02)
        time.sleep(.12)
        self.send('touch', x=x, y=end, pressed=False)
        time.sleep(.16)


def prepare(folder, theme=None, scale=100, reading=100):
    fixture(folder)
    with sqlite3.connect(folder/'penchat.db') as db:
        settings = {'auto_title_enabled': False, 'ui_scale_percent': scale,
                    'reading_font_percent': reading}
        if theme is not None:
            settings['theme_mode'] = theme
        for key, value in settings.items():
            db.execute('insert or replace into app_settings values(?,?,1)', (key, json.dumps(value)))
    updates = folder/'.updates'
    updates.mkdir()
    app = json.loads((t.ROOT/'package.json').read_text())
    (updates/'restore.json').write_text(json.dumps({'appid': app['appid'], 'target': app['version'],
        'snapshot': {'session': 'fixture', 'draft': '保留草稿 theme Aa', 'preferences': {'model_id': 'a'},
                     'scroll': 0, 'offset': 0, 'following': True}}))
    (folder/'feedback.json').write_text('{}')
    (folder/'update.json').write_text(json.dumps({'root': 'http://127.0.0.1:1/penchat-rk/',
                                                'nodes': ['127.0.0.1'], 'auto': False}))
    os.environ['PENCHAT_TEST_FEEDBACK_CONFIG'] = str(folder/'feedback.json')
    os.environ['PENCHAT_TEST_UPDATE_CONFIG'] = str(folder/'update.json')


def start(folder, size=(960, 266), **kwargs):
    w, h = size
    os.environ['PENCHAT_TEST_Y07_ROUNDED'] = '1' if h > w else '0'
    return App(BINARY, folder, seed_model=False, official_adapter=False,
               screen={'width': w, 'height': h, 'direction': 0}, **kwargs)


def choose_theme(a, theme):
    a.setting('theme')
    d = a.diag()
    assert d['context_menu']['kind'] == 'choice:theme'
    assert set(d['context_menu']['choices']) == {'light', 'dark'}
    assert all(row['bounds'][3]-row['bounds'][1]+1 >= 44
               for row in d['context_menu']['choices'].values())
    select(a, theme)


def wait_theme(a, theme):
    t.wait_for(lambda: a.diag()['appearance'] == {'theme': theme, 'pending': False, 'ready': False})


def stored(folder):
    with sqlite3.connect(folder/'penchat.db') as db:
        row = db.execute("select value_json from app_settings where key='theme_mode'").fetchone()
        return json.loads(row[0]) if row else None


def behavior():
    OUT.mkdir(parents=True, exist_ok=True)
    for raw in ['missing', None, 'invalid', False, 17, {}, ['light']]:
        with tempfile.TemporaryDirectory(prefix='penchat-theme-default-') as temp:
            folder = pathlib.Path(temp)
            prepare(folder, None if raw == 'missing' else raw)
            if raw is None:
                with sqlite3.connect(folder/'penchat.db') as db:
                    db.execute("insert or replace into app_settings values('theme_mode','null',1)")
            a = start(folder)
            try:
                assert a.diag()['appearance']['theme'] == 'dark', raw
            finally:
                a.close()
    print('THEME_DEFAULT_AND_INVALID_STORAGE_DARK', flush=True)

    for size, scale in [((960, 266), 100), ((640, 260), 75), ((568, 1210), 150)]:
        with tempfile.TemporaryDirectory(prefix='penchat-theme-settings-') as temp:
            folder = pathlib.Path(temp)
            prepare(folder, scale=scale, reading=125)
            a = start(folder, size)
            try:
                before = a.diag()
                open_settings(a)
                assert len(a.diag()['settings']['rows']) == 27
                for theme in ['light', 'dark', 'light']:
                    choose_theme(a, theme)
                    wait_theme(a, theme)
                    assert stored(folder) == theme
                    d = a.diag()
                    assert d['view'] == 'settings' and d['ui_scale']['applied'] == scale
                    assert d['reading_font']['percent'] == 125
                    back(a)
                    t.wait_for(lambda: a.diag()['view'] == 'chat')
                    d = a.diag()
                    assert d['session'] == before['session'] and d['draft_length'] == before['draft_length']
                    assert d['active_model'] == before['active_model']
                    assert d['following_latest'] == before['following_latest']
                    open_settings(a)
                with sqlite3.connect(folder/'penchat.db') as db:
                    db.execute("create trigger reject_theme before insert on app_settings when NEW.key='theme_mode' begin select raise(abort,'fixture write failure'); end")
                choose_theme(a, 'dark')
                t.wait_for(lambda: not a.diag()['appearance']['pending'])
                assert a.diag()['appearance']['theme'] == 'light' and stored(folder) == 'light'
                capture(a, OUT/f'{size[0]}x{size[1]}-save-failure.png')
                with sqlite3.connect(folder/'penchat.db') as db:
                    db.execute('drop trigger reject_theme')
                choose_theme(a, 'dark')
                wait_theme(a, 'dark')
            finally:
                a.close()
            a = start(folder, size)
            try:
                assert a.diag()['appearance']['theme'] == 'dark'
                assert a.diag()['ui_scale']['applied'] == scale and a.diag()['reading_font']['percent'] == 125
            finally:
                a.close()
        print('THEME_CLICK_SAVE_FAILURE_RETRY_RESTART_STATE', *size, flush=True)
    deferred()
    anchor()
    stream()


def deferred():
    for lifecycle in ['focus', 'background']:
        with tempfile.TemporaryDirectory(prefix='penchat-theme-deferred-') as temp:
            folder = pathlib.Path(temp)
            prepare(folder)
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute('create table theme_audit(value text)')
                db.execute("create trigger theme_audit_write after insert on app_settings when NEW.key='theme_mode' begin insert into theme_audit values(NEW.value_json); end")
            a = start(folder)
            lock = None
            try:
                open_settings(a)
                a.setting('theme')
                time.sleep(.18)
                b = a.diag()['context_menu']['choices']['light']['bounds']
                lock = sqlite3.connect(folder/'penchat.db')
                lock.execute('begin immediate')
                tap(a, b, wait_motion=False)
                # A repeated physical submission during the pending write must
                # neither queue another update nor navigate into another row.
                tap(a, b, wait_motion=False)
                if lifecycle == 'focus':
                    a.send('window.focus', sequence=1, focused=False)
                else:
                    a.send('visibility', visible=False)
                time.sleep(.1)
                lock.commit()
                lock.close()
                lock = None
                t.wait_for(lambda: a.diag()['appearance']['ready'])
                assert a.diag()['appearance'] == {'theme': 'dark', 'pending': True, 'ready': True}
                assert stored(folder) == 'light'
                if lifecycle == 'focus':
                    for seq in range(2, 8):
                        a.send('window.focus', sequence=seq, focused=True)
                        time.sleep(.05)
                else:
                    a.send('visibility', visible=True)
                wait_theme(a, 'light')
                assert a.diag()['view'] == 'settings'
                with sqlite3.connect(folder/'penchat.db') as db:
                    assert db.execute('select value from theme_audit').fetchall() == [('"light"',)]
            finally:
                if lock:
                    lock.close()
                a.close()
        print('THEME_REPEATED_SUBMIT_ACK_DEFERRED', lifecycle, flush=True)


def anchor():
    with tempfile.TemporaryDirectory(prefix='penchat-theme-anchor-') as temp:
        folder = pathlib.Path(temp)
        prepare(folder)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute("update chat_messages set output_text=? where id='fixture2'", ('\n\n'.join(f'第 {i} 段阅读锚点。中文 English 保持。'*5 for i in range(60)),))
        a = start(folder)
        try:
            for _ in range(4):
                a.settings_swipe(110)
            before = a.diag()
            assert not before['following_latest'] and before['scroll_y'] > 0
            for theme in ['light', 'dark']:
                open_settings(a)
                held = a.diag()['ui_scale']['anchor']
                choose_theme(a, theme)
                wait_theme(a, theme)
                back(a)
                t.wait_for(lambda: a.diag()['view'] == 'chat')
                d = a.diag()
                assert d['session'] == before['session'] and not d['following_latest']
                assert abs(d['scroll_y']-before['scroll_y']) <= 2, (before['scroll_y'], d['scroll_y'], held)
                assert d['draft_length'] == before['draft_length']
        finally:
            a.close()
    print('THEME_READING_ANCHOR_AND_DRAFT', flush=True)


class Stream(http.server.BaseHTTPRequestHandler):
    release = threading.Event()
    def log_message(self, *args):
        pass
    def do_POST(self):
        json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Connection', 'close')
        self.end_headers()
        def emit(v):
            self.wfile.write(('data: '+json.dumps(v)+'\n\n').encode())
            self.wfile.flush()
        emit({'type': 'response.output_text.delta', 'delta': '第一部分。\n\n**正文** $x^2$。'})
        self.release.wait(30)
        emit({'type': 'response.output_text.delta', 'delta': '最后一部分，完整保留。'})
        emit({'type': 'response.completed'})
        self.close_connection = True


def stream():
    Stream.release.clear()
    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Stream) as server, tempfile.TemporaryDirectory(prefix='penchat-theme-stream-') as temp:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        folder = pathlib.Path(temp)
        prepare(folder)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute('update providers set base_url=?', (f'http://127.0.0.1:{server.server_port}/v1',))
        a = t.Native(BINARY, folder)
        try:
            a.edit('开始流式主题测试')
            a.tap(908, 232)
            t.wait_for(lambda: a.diag()['generating'])
            a.edit('保留后续草稿')
            session = a.diag()['session']
            open_settings(a)
            choose_theme(a, 'light')
            wait_theme(a, 'light')
            assert a.diag()['generating'] and a.diag()['session'] == session
            back(a)
            Stream.release.set()
            t.wait_for(lambda: not a.diag()['generating'], 15)
            d = a.diag()
            assert d['draft_length'] == len('保留后续草稿') and d['session'] == session
            with sqlite3.connect(folder/'penchat.db') as db:
                rows = db.execute('select role,status,output_text from chat_messages where session_id=? order by seq', (session,)).fetchall()
            assert len(rows) == 4 and rows[-1][2].endswith('最后一部分，完整保留。'), rows
        finally:
            Stream.release.set()
            a.close()
            server.shutdown()
    print('THEME_STREAM_RETAINS_SESSION_DRAFT_AND_REPLY', flush=True)


def matrix(sizes=None):
    OUT.mkdir(parents=True, exist_ok=True)
    index = []
    for w, h in sizes or [(960, 266), (640, 260), (568, 1210), (1280, 266)]:
        for theme in ['dark', 'light']:
            for scale in [75, 100, 125, 150]:
                prefix = f'{w}x{h}-{theme}-{scale}'
                with tempfile.TemporaryDirectory(prefix='penchat-theme-matrix-') as temp:
                    folder = pathlib.Path(temp)
                    prepare(folder, theme, scale)
                    a = start(folder, (w, h))
                    def shot(kind):
                        name = f'{prefix}-{kind}.png'
                        capture(a, OUT/name)
                        index.append(name)
                    try:
                        assert a.diag()['appearance']['theme'] == theme
                        assert a.diag()['ui_scale']['applied'] == scale
                        tap(a, a.diag()['composer_text_bounds'])
                        t.wait_for(lambda: a.diag()['editor']['active'] and a.diag()['editor']['keys'])
                        shot('keyboard')
                        a.editor_action('done')
                        t.wait_for(lambda: not a.diag()['editor']['active'])
                        # Actual rich renderer contains heading, reasoning,
                        # prose, math, table and a syntax-highlighted code block.
                        for _ in range(45):
                            if a.diag()['scroll_y'] <= 0:
                                break
                            a.settings_swipe(120)
                        shot('chat')
                        for _ in range(40):
                            d = a.diag()
                            tables = d.get('table_rows', [])
                            if tables and 15 <= tables[0]['y'] <= h//2:
                                break
                            a.settings_swipe(-100)
                        shot('rich')
                        open_settings(a)
                        a.setting('theme')
                        time.sleep(.2)
                        shot('theme-menu')
                        select(a, theme)
                        shot('settings')
                        back(a)
                        t.wait_for(lambda: a.diag()['view'] == 'chat')
                    finally:
                        a.close()
                print('THEME_VISUAL_MATRIX', prefix, flush=True)
    index_file = OUT/'index.json'
    previous = json.loads(index_file.read_text()) if index_file.exists() else []
    index_file.write_text(json.dumps(sorted(set(previous+index)), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--matrix', action='store_true')
    args = parser.parse_args()
    matrix() if args.matrix else behavior()
