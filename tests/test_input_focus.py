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
# A system window can take focus while LVGL is elastically pulled, before release.
# Resetting just the indev used to strand negative page scroll indefinitely.
for width,height in [(640,260),(960,266)]:
    with tempfile.TemporaryDirectory(prefix='penchat-focus-held-') as directory:
        app=Native(binary,directory,seed_model=False,official_adapter=False,
                   screen={'width':width,'height':height,'direction':0})
        sequence=0
        try:
            focus(True);app.open_settings()
            for cycle in range(3):
                focus(True)
                app.send('touch',x=width//2,y=5,pressed=True)
                for y in [30,65,100,145,190]:
                    app.send('touch',x=width//2,y=y,pressed=True);time.sleep(.03)
                assert app.diag()['scroll_y']<0,'Fixture must actually stretch the page'
                focus(False);time.sleep(.05)
                state=app.diag();assert state['view']=='settings' and state['scroll_y']==0,state['scroll_y']
                # Held movement, release and stale focus must not restart the old drag.
                app.send('touch',x=width//2,y=240,pressed=True)
                app.send('touch',x=width//2,y=240,pressed=False)
                assert app.diag()['scroll_y']==0
            focus(True)
            app.settings_swipe(-100);time.sleep(.4)
            before=app.diag()['scroll_y'];assert before>0
            focus(False);assert app.diag()['scroll_y']==before,'Valid reading position changed'
        finally:app.close()
print('Held top-panel drag: elastic overflow settles, stale movement is discarded, valid reading position preserved.')
