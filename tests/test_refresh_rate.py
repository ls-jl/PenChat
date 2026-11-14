#!/usr/bin/env python3
"""Refresh-rate controls, actual display timer and durable/lifecycle state.

Every process uses a private fixture DB and localhost-only fake provider. No
device access, screenshots, production credentials or full regression suite.
"""
import http.server
import json
import pathlib
import sqlite3
import tempfile
import threading
import time

import test_lvgl_app as t
from choice_ui import select
from test_theme_settings import BINARY, Stream, prepare, start
from ui_scale_ui import back, open_settings, tap

SIZES = [(960, 266), (640, 260), (568, 1210)]
KEY = 'ui_refresh_hz'
MISSING = object()


def set_raw(folder, raw):
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('delete from app_settings where key=?', (KEY,))
        if raw is not MISSING:
            db.execute('insert into app_settings values(?,?,1)', (KEY, raw))


def stored(folder):
    with sqlite3.connect(folder/'penchat.db') as db:
        row = db.execute('select value_json from app_settings where key=?', (KEY,)).fetchone()
        return json.loads(row[0]) if row else None


def check_timer(a, hz, idle=True):
    d = a.diag()
    rate = d['refresh_rate']
    assert rate['applied'] == hz, rate
    # This is the LVGL refresh timer's period, rather than a menu caption or
    # a second application-side copy of the requested value.
    assert rate['period_ms'] == 1000//hz, rate
    assert rate['animation_period_ms'] == 1000//hz, rate
    assert rate['input_period_ms'] == 1000//hz, rate
    assert rate['default_hz'] == (60 if d['height'] > d['width'] else 30), rate
    if idle:
        assert not rate['pending'] and not rate['ready'], rate
    return d


def choose(a, hz):
    a.setting('refresh_rate')
    d = a.diag()
    assert d['context_menu']['kind'] == 'choice:refresh_rate', d['context_menu']
    assert set(d['context_menu']['choices']) == {'30', '60'}
    for row in d['context_menu']['choices'].values():
        assert row['bounds'][3]-row['bounds'][1]+1 >= 44, row
    select(a, str(hz))


def settled(a, hz):
    t.wait_for(lambda: a.diag()['refresh_rate']['applied'] == hz and
               not a.diag()['refresh_rate']['pending'])
    return check_timer(a, hz)


def defaults():
    # Strict integer 30/60 only. Floats, strings, bool, JSON containers and
    # unsupported integer values retain the current orientation default.
    values = [MISSING, 'null', 'false', '"60"', '30.0', '{}', '[]',
              '0', '45', '-30', '4294967326', '18446744073709551615', '30', '60']
    for size in [SIZES[0], SIZES[2]]:
        with tempfile.TemporaryDirectory(prefix='penchat-refresh-default-') as temp:
            folder = pathlib.Path(temp)
            prepare(folder)
            for raw in values:
                set_raw(folder, raw)
                a = start(folder, size)
                try:
                    expected = int(raw) if raw in ('30', '60') else (60 if size[1] > size[0] else 30)
                    d = check_timer(a, expected)
                    assert d['refresh_rate']['configured'] == (expected if raw in ('30', '60') else 0)
                finally:
                    a.close()
        print('REFRESH_ORIENTATION_DEFAULT_STRICT_INTEGER_STORAGE', *size, flush=True)


def controls():
    for size in SIZES:
        for theme in ['dark', 'light']:
            with tempfile.TemporaryDirectory(prefix='penchat-refresh-controls-') as temp:
                folder = pathlib.Path(temp)
                prepare(folder, theme=theme, reading=125)
                a = start(folder, size)
                try:
                    before = a.diag()
                    assert before['draft_length'] > 0
                    open_settings(a)
                    assert len(a.diag()['settings']['rows']) == 26
                    default_hz = before['refresh_rate']['default_hz']
                    # Clicking an unchanged orientation default still records
                    # an explicit choice for future orientation changes.
                    for hz in [default_hz, 60, 30, 60]:
                        # Opening the picker may scroll to its row. Saving a
                        # rate must leave that reader, its geometry and scroll.
                        a.setting('refresh_rate')
                        held = a.diag()
                        select(a, str(hz))
                        d = settled(a, hz)
                        assert stored(folder) == hz
                        assert d['refresh_rate']['configured'] == hz
                        expected_changes = held['refresh_rate']['changes'] + (held['refresh_rate']['applied'] != hz)
                        assert d['refresh_rate']['changes'] == expected_changes
                        assert d['view'] == 'settings' and d['scroll_y'] == held['scroll_y']
                        assert d['settings']['rows'] == held['settings']['rows']
                        assert d['appearance']['theme'] == theme
                        assert d['reading_font']['percent'] == 125
                        assert d['session'] == before['session'] and d['active_model'] == before['active_model']
                        assert d['generation'] == before['generation']
                        assert d['draft_length'] == before['draft_length']
                    # A rejected async write cannot change the active timer.
                    with sqlite3.connect(folder/'penchat.db') as db:
                        db.execute("create trigger reject_refresh before insert on app_settings when NEW.key='ui_refresh_hz' begin select raise(abort,'fixture write failure'); end")
                    choose(a, 30)
                    t.wait_for(lambda: not a.diag()['refresh_rate']['pending'])
                    check_timer(a, 60)
                    assert a.diag()['refresh_rate']['error'] is True
                    assert stored(folder) == 60
                    with sqlite3.connect(folder/'penchat.db') as db:
                        db.execute('drop trigger reject_refresh')
                    choose(a, 30)
                    settled(a, 30)
                    assert a.diag()['refresh_rate']['error'] is False
                    back(a)
                    t.wait_for(lambda: a.diag()['view'] == 'chat')
                    d = a.diag()
                    assert d['draft_length'] == before['draft_length'] and d['session'] == before['session']
                    assert d['following_latest'] == before['following_latest']
                finally:
                    a.close()
                # An explicit rate overrides the opposite orientation default
                # as well as surviving a normal native restart.
                opposite = SIZES[0] if size[1] > size[0] else SIZES[2]
                a = start(folder, opposite)
                try:
                    check_timer(a, 30)
                    assert stored(folder) == 30
                finally:
                    a.close()
            print('REFRESH_REAL_CONTROLS_FAILURE_RETRY_RESTART_DRAFT', *size, theme, flush=True)


def deferred():
    for lifecycle in ['focus', 'background']:
        with tempfile.TemporaryDirectory(prefix='penchat-refresh-deferred-') as temp:
            folder = pathlib.Path(temp)
            prepare(folder)
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute('create table refresh_audit(value text)')
                db.execute("create trigger refresh_audit_write after insert on app_settings when NEW.key='ui_refresh_hz' begin insert into refresh_audit values(NEW.value_json); end")
            a = start(folder)
            lock = None
            try:
                before = a.diag()
                open_settings(a)
                a.setting('refresh_rate')
                time.sleep(.18)
                b = a.diag()['context_menu']['choices']['60']['bounds']
                lock = sqlite3.connect(folder/'penchat.db')
                lock.execute('begin immediate')
                tap(a, b)
                tap(a, b)
                # A diagnostic also reads credential-free model/settings data
                # on the DB worker. Do not queue that synchronous reader behind
                # the deliberately blocked write before releasing this lock.
                if lifecycle == 'focus':
                    a.send('window.focus', sequence=1, focused=False)
                else:
                    a.send('visibility', visible=False)
                time.sleep(.1)
                lock.commit()
                lock.close()
                lock = None
                t.wait_for(lambda: a.diag()['refresh_rate']['ready'])
                d = check_timer(a, 30, idle=False)
                assert d['refresh_rate']['pending'] and d['refresh_rate']['ready']
                assert stored(folder) == 60
                if lifecycle == 'focus':
                    for sequence in range(2, 8):
                        a.send('window.focus', sequence=sequence, focused=True)
                        time.sleep(.05)
                else:
                    a.send('visibility', visible=True)
                d = settled(a, 60)
                assert d['view'] == 'settings' and not d['modal']
                assert d['session'] == before['session'] and d['draft_length'] == before['draft_length']
                with sqlite3.connect(folder/'penchat.db') as db:
                    assert db.execute('select value from refresh_audit').fetchall() == [('60',)]
            finally:
                if lock:
                    lock.close()
                a.close()
        print('REFRESH_REPEATED_TAP_SINGLE_WRITE_ACK_DEFERRED', lifecycle, flush=True)


def reading_position():
    with tempfile.TemporaryDirectory(prefix='penchat-refresh-reading-') as temp:
        folder = pathlib.Path(temp)
        prepare(folder)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute("update chat_messages set output_text=? where id='fixture2'",
                       ('\n\n'.join(f'第 {i} 段阅读位置保持。中文 English。'*5 for i in range(60)),))
        a = start(folder)
        try:
            for _ in range(4):
                a.settings_swipe(110)
            before = a.diag()
            assert not before['following_latest'] and before['scroll_y'] > 0
            for hz in [60, 30]:
                open_settings(a)
                choose(a, hz)
                settled(a, hz)
                back(a)
                t.wait_for(lambda: a.diag()['view'] == 'chat')
                d = a.diag()
                assert d['session'] == before['session'] and not d['following_latest']
                assert abs(d['scroll_y']-before['scroll_y']) <= 2, (before['scroll_y'], d['scroll_y'])
                assert d['draft_length'] == before['draft_length']
        finally:
            a.close()
    print('REFRESH_READING_POSITION_AND_DRAFT_PRESERVED', flush=True)


def streaming():
    Stream.release.clear()
    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Stream) as server, tempfile.TemporaryDirectory(prefix='penchat-refresh-stream-') as temp:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        folder = pathlib.Path(temp)
        prepare(folder)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute('update providers set base_url=?', (f'http://127.0.0.1:{server.server_port}/v1',))
        a = t.Native(BINARY, folder)
        try:
            a.edit('刷新率变化时保留流式回复')
            a.tap(908, 232)
            t.wait_for(lambda: a.diag()['generating'])
            a.edit('保留后续草稿')
            before = a.diag()
            open_settings(a)
            for hz in [60, 30]:
                choose(a, hz)
                d = settled(a, hz)
                assert d['generating'] and d['session'] == before['session']
                assert d['generation'] == before['generation']
                assert d['draft_length'] == before['draft_length']
            back(a)
            Stream.release.set()
            t.wait_for(lambda: not a.diag()['generating'], 15)
            d = a.diag()
            assert d['draft_length'] == len('保留后续草稿') and d['session'] == before['session']
            with sqlite3.connect(folder/'penchat.db') as db:
                rows = db.execute('select role,status,output_text from chat_messages where session_id=? order by seq', (before['session'],)).fetchall()
            assert len(rows) == 4 and rows[-1][1] == 'complete'
            assert rows[-1][2].endswith('最后一部分，完整保留。'), rows
        finally:
            Stream.release.set()
            a.close()
            server.shutdown()
    print('REFRESH_STREAM_SESSION_GENERATION_DRAFT_AND_FINAL_REPLY', flush=True)


if __name__ == '__main__':
    defaults()
    controls()
    deferred()
    reading_position()
    streaming()
    print('REFRESH_RATE_EXTERNAL_HOST_CONTRACT_OK', flush=True)
