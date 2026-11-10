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
            assert not s['touch_input']['active'] and not editor(app)['ocr_active']
            capture(app,OUT/f'{w}-official-upper.png')
            app.send('keyboard.change',requestId=ident,text='fixture',cursorIndex=7)
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
            assert not app.diag()['display_visible'] and editor(app)['length']==0
            app.send('window.focus',focused=True,sequence=2)
            ui.wait_for(lambda:app.diag()['display_visible'])
            assert editor(app)['active'] and not app.diag()['display_top_height']
            print('Official retained page: IME focus, clipping, expiry, stale IDs, overlay, background and cancel:',w,h,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(568,1210),(600,1280)]:run(*size)
