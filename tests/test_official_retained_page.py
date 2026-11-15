#!/usr/bin/env python3
"""A bounded official-IME display lease retains only the native upper page."""
import pathlib,tempfile,time
import test_lvgl_app as ui
from test_pinyin_ui import BINARY,open_editor,editor
from image_gestures import capture
ui.port=1
OUT=ui.ROOT/'.work/official-retained';OUT.mkdir(parents=True,exist_ok=True)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-official-retained-') as tmp:
        app=ui.Native(BINARY,pathlib.Path(tmp),official_adapter=False,screen={'width':w,'height':h})
        try:
            open_editor(app);app.editor_action('official');request=app.receive('keyboard.open');ident=request['requestId']
            app.send('window.focus',focused=False,sequence=1)
            assert not app.diag()['display_visible']
            def layout(foreground=True,request=ident,height=h//2):
                app.send('keyboard.layout',requestId=request,height=height,foreground=foreground)
            layout();s=app.diag()
            assert s['display_visible'] and not s['window_focused']
            assert s['display_top_height']==h-h//2 and s['composer_bounds'][3]<s['display_top_height']
            assert s['chat_header']['reader_bounds'][3]==h-1
            assert s['chat_header']['reader_bounds'][3]>s['composer_bounds'][3]
            assert not s['touch_input']['active'] and not editor(app)['ocr_active']
            assert editor(app)['text_visible'] and editor(app)['text_in_target']
            capture(app,OUT/f'{w}-official-upper.png')
            app.send('keyboard.change',requestId=ident,text='fixture',cursorIndex=7)
            assert editor(app)['length']==7 and editor(app)['text_visible']
            for _ in range(3):app.send('keyboard.change',requestId=ident,text='fixture',cursorIndex=3)
            assert editor(app)['cursor']==3,'Unchanged cursor heartbeat moved insertion point'
            app.send('keyboard.change',requestId='stale',text='ignored',cursorIndex=0)
            assert editor(app)['length']==7
            time.sleep(.82);assert not app.diag()['display_visible'],'Expired monitor must release its plane'
            layout(request='stale');assert not app.diag()['display_visible']
            layout();assert app.diag()['display_visible']
            app.send('system.overlay.status',panel_visible=True)
            ui.wait_for(lambda:not app.diag()['display_visible'])
            layout();assert not app.diag()['display_visible']
            app.send('system.overlay.status',panel_visible=False);layout()
            assert app.diag()['display_visible']
            app.send('visibility',visible=False);layout();assert not app.diag()['display_visible']
            app.send('visibility',visible=True);layout();assert app.diag()['display_visible']
            app.send('keyboard.result',requestId=ident,confirmed=False)
            assert not app.diag()['display_visible'] and editor(app)['length']==7
            app.send('window.focus',focused=True,sequence=2)
            ui.wait_for(lambda:app.diag()['display_visible'])
            assert editor(app)['active'] and not app.diag()['display_top_height']
            app.editor_action('official');reopened=app.receive('keyboard.open')['requestId']
            assert reopened!=ident
            app.send('keyboard.change',requestId=ident,text='stale',cursorIndex=5)
            assert editor(app)['length']==7
            app.send('keyboard.change',requestId=reopened,text='confirmed',cursorIndex=9)
            assert editor(app)['length']==9 and editor(app)['text_visible']
            app.send('keyboard.result',requestId=reopened,confirmed=True,text='confirmed')
            assert not editor(app)['active']
            print('Official retained page: IME focus, clipping, expiry, stale IDs, overlay, background, proxy mirror, cancel/reopen/confirm:',w,h,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(568,1210),(600,1280)]:run(*size)
