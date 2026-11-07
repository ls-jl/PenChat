#!/usr/bin/env python3
"""Actual LVGL process: overlays cancel clicks without destroying page state."""
import os, pathlib, tempfile, time
from test_lvgl_app import Native, ROOT
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
with tempfile.TemporaryDirectory(prefix='penchat-focus-') as directory:
    app=Native(binary,directory,seed_model=False,official_adapter=False)
    sequence=0
    def focus(active):
        global sequence
        sequence+=1
        app.send('input.focus',sequence=sequence,active=active)
    try:
        focus(True);app.tap(40,32)
        assert app.diag()['navigation_open']
        focus(False);app.tap(300,180)
        assert app.diag()['navigation_open'] and not app.diag()['input_focused']
        # An old active message cannot reenable the underlying page.
        app.send('input.focus',sequence=sequence-1,active=True)
        app.tap(800,120);assert app.diag()['navigation_open']
        focus(True);app.tap(800,120);assert not app.diag()['navigation_open']
        # Cover arriving between press and release must cancel, not click menu.
        focus(True);app.send('touch',x=40,y=32,pressed=True);time.sleep(.06)
        focus(False);app.send('touch',x=40,y=32,pressed=False);time.sleep(.06)
        assert not app.diag()['navigation_open']
        # A stalled launcher fails closed even without onHide.
        focus(True);time.sleep(.38);app.tap(40,32)
        state=app.diag();assert not state['navigation_open'] and not state['input_focused']
        assert state['visible'] and state['display_visible']
        time.sleep(1.3);assert not app.diag()['display_visible']
        focus(True);app.tap(40,32);assert app.diag()['navigation_open']
    finally:app.close()
print('Actual LVGL focus: overlay, stale messages, cancelled release, expiry and recovery passed.')
