#!/usr/bin/env python3
"""Disabled send circles retain their hit area instead of focusing the capsule."""
import os
import pathlib
import tempfile
import test_lvgl_app as t

t.port = 1
BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))
for width, height in ((960, 266), (640, 260), (568, 1210)):
    with tempfile.TemporaryDirectory(prefix='penchat-empty-send-') as folder:
        app = t.Native(BINARY, folder, screen={'width': width, 'height': height})
        try:
            for draft in ('', ' \n\t '):
                if draft:
                    bounds = app.diag()['composer_text_bounds']
                    app.tap((bounds[0]+bounds[2])//2, (bounds[1]+bounds[3])//2)
                    request = app.receive('keyboard.open')
                    app.send('keyboard.result', requestId=request['requestId'], confirmed=True, text=draft)
                    t.wait_for(lambda: not app.diag()['keyboard'] and not app.diag()['editor']['active'] and not app.diag()['surface_motion'])
                before = app.diag()
                for _ in range(2):
                    app.tap(before['send_center_x2']//2, before['send_center_y2']//2)
                    after = app.diag()
                    assert not after['editor']['active'] and not after['keyboard'], after['editor']
                    assert not after['generating'] and not app.read_messages()
                    assert after['draft_length'] == len(draft)
                    assert after['session'] == before['session'] and after['summary']['sessions'] == 0
                # The neighboring text area must remain independently usable.
                bounds = app.diag()['composer_text_bounds']
                app.tap((bounds[0]+bounds[2])//2, (bounds[1]+bounds[3])//2)
                t.wait_for(lambda: app.diag()['editor']['active'])
                app.editor_action('done')
                t.wait_for(lambda: not app.diag()['editor']['active'] and not app.diag()['surface_motion'])
            print(f'{width}x{height}: empty/whitespace send consumes touches; draft, session and editor focus preserved.', flush=True)
        finally:
            app.close()
