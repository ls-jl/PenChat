#!/usr/bin/env python3
"""Transient system overlays yield display/input without resetting page state."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/window-focus';OUT.mkdir(parents=True,exist_ok=True)
for width,height in [(960,266),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-window-focus-') as folder:
        seed(folder,'\n\n'.join('保留阅读位置，第'+str(i)+'段。' for i in range(70)))
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height})
        try:
            sequence=0
            def focus(value):
                global sequence
                sequence+=1;a.send('window.focus',sequence=sequence,focused=value)
                t.wait_for(lambda:a.diag()['window_focused']==value)
            if height>width:
                before=a.diag()['scroll_y']
                for y in [2,35,80,160,240]:a.send('touch',x=width//2,y=y,pressed=True);time.sleep(.03)
                a.send('touch',x=width//2,y=240,pressed=False)
                assert a.diag()['scroll_y']==before and not a.diag()['reader_interacting']
                # A real panel can open before the starting finger releases.
                # Touch arbitration must not swallow these non-touch packets.
                a.send('touch',x=width//2,y=2,pressed=True)
                a.send('system.overlay.status',panel_visible=True,panel_supported=True)
                assert a.diag()['system_overlay']['panel_visible']
                focus(False);assert not a.diag()['input_focused'] and not a.diag()['display_visible']
                focus(True);a.send('touch',x=width//2,y=240,pressed=False)
                assert a.diag()['scroll_y']==before
            a.open_navigation();before=a.diag();focus(False);s=a.diag()
            assert s['page_visible'] and not s['input_focused'] and not s['display_visible'] and s['navigation_open']
            a.tap(width-10,60);assert a.diag()['navigation_open']
            a.send('window.focus',sequence=0,focused=True);assert not a.diag()['window_focused']
            focus(True);s=a.diag();assert s['navigation_open'] and s['session']==before['session'] and s['scroll_y']==before['scroll_y']
            a.tap(width-10,60);b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            t.wait_for(lambda:a.diag()['editor']['active'] and bool(a.diag()['editor']['keys']))
            for letter in 'ni':
                key=next(k for k in a.diag()['editor']['keys'] if k['key']==letter);a.tap(key['x'],key['y'])
            before=a.diag()['editor'];assert before['preedit_length']==2
            focus(False);s=a.diag();assert s['editor']['active'] and not s['editor']['ocr_active'] and not s['display_visible']
            key=next(k for k in before['keys'] if k['key']=='a');a.tap(key['x'],key['y']);assert a.diag()['editor']['preedit_length']==2
            focus(True);s=a.diag();after=s['editor']
            assert s['display_visible'] and after['session']==before['session'] and after['keyboard_session']==before['keyboard_session'] and after['preedit_length']==2
            capture(a,OUT/f'{width}-restored-editor.png')
            if height>width:
                key=next(k for k in after['keys'] if k['key']=='language');a.tap(key['x'],key['y'])
                assert a.diag()['editor']['parked_count']==1
            a.editor_action('official');request=a.receive('keyboard.open');focus(False)
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            assert not a.diag()['display_visible']
            focus(True);assert a.diag()['editor']['active'] and a.diag()['display_visible']
            focus(False);a.send('visibility',visible=False);focus(True)
            assert not a.diag()['visible'] and not a.diag()['page_visible']
            a.send('visibility',visible=True);assert a.diag()['visible'] and a.diag()['display_visible']
            print(f'Window focus {width}x{height}: display/touch handoff, stale sequence, drawer/scroll/input preservation, official fallback and real background precedence passed.',flush=True)
        finally:a.close()
