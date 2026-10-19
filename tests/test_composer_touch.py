#!/usr/bin/env python3
"""Composer opens only for a tap, never for a swipe or cancelled pointer."""
import os, pathlib, tempfile, time
from test_lvgl_app import Native, ROOT
import test_lvgl_app as transport
transport.port = 1
with tempfile.TemporaryDirectory(prefix='penchat-composer-') as directory:
    app = Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', ROOT/'build/host/penchat-lvgl')), pathlib.Path(directory))
    try:
        for points, cancelled in [
            ([(500,232),(550,232),(500,232)], False),
            ([(500,232),(500,215),(500,232)], False),
            ([(500,180),(500,232)], False),
            ([(500,232)], True),
        ]:
            for x,y in points:
                app.send('touch', x=x, y=y, pressed=True)
                time.sleep(.08)
            app.send('touch', x=x, y=y, pressed=False, cancelled=cancelled)
            time.sleep(.15)
            assert not app.diag()['keyboard'], (points, cancelled)
        app.tap(500,232)
        request=app.receive('keyboard.open')
        app.send('keyboard.result', requestId=request['requestId'], confirmed=True, text='保留草稿')
        time.sleep(.15)
        app.tap(500,232)
        request=app.receive('keyboard.open')
        app.send('keyboard.result', requestId=request['requestId'], confirmed=False, text='')
    finally:
        app.close()
print('Composer: horizontal/vertical return swipes, outside origin, cancel and subsequent taps passed.')
