#!/usr/bin/env python3
"""Composer opens only for a tap, never for a swipe or cancelled pointer."""
import os, pathlib, tempfile, time
from test_lvgl_app import Native, ROOT
from test_image_viewer import shot
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
            state=app.diag()
            assert not state['keyboard'] and not state['editor']['active'], (points, cancelled)
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

# The enlarged plus target owns its former boundary with the editor. Its
# visual center stays fixed, and a deliberate text tap still opens editing.
for width,height in [(960,266),(936,280)]:
    with tempfile.TemporaryDirectory(prefix='penchat-attachment-hit-') as directory:
        app=Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl')),directory,official_adapter=False,screen={'width':width,'height':height})
        try:
            for x,y in [(154,height-34),(158,height-34),(154,height-12),(108,height-56)]:
                app.tap(x,y)
                state=app.diag()
                assert state['attachments'] and not state['editor']['active'],(width,x,y,state)
                if x==158:shot(app,f'composer-hit-menu-{width}')
                app.tap(width-30,80)
                assert not app.diag()['attachments']
            shot(app,f'composer-hit-{width}')
            app.tap(180,height-34)
            assert app.diag()['editor']['active']
        finally:app.close()
print('Attachment hit target: enlarged edges avoid IME; adjacent entry remains usable at both screen sizes.')
