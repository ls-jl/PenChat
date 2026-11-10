#!/usr/bin/env python3
"""Regression: endpoints do not disappear at the anchor, wraps or trailing newlines."""
import pathlib
import sqlite3
import tempfile
import time
import test_lvgl_app as ui
from test_text_selection import BINARY, fixture, ro, press, bounds_tap, editor, screenshot


def run(width,height,code=False):
    with tempfile.TemporaryDirectory(prefix='penchat-selection-handles-') as tmp:
        folder=pathlib.Path(tmp);fixture(folder)
        if code:
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute("UPDATE chat_messages SET output_text=? WHERE id='a'",('```text\nAlpha\nbeta\n\n```',))
        app=ui.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height,'direction':0})
        try:
            r=next(r for r in ro(app)['regions'] if r['key']=='a');b=r['parts'][0]
            x=b[0]+14;y=b[1]+14
            app.long_press(x,y,.52);assert ro(app)['selected']
            if code:
                m=ro(app)['menu_bounds'];app.tap(m[0]+45,m[1]+25)
                assert len(ro(app)['handles'])==2,ro(app)
                screenshot(app,str(width)+'-code-end-handles')
            else:
                h=ro(app)['handles'][-1];hx=(h[0]+h[2])//2;hy=(h[1]+h[3])//2
                press(app,hx,hy);press(app,b[0]+6,y);time.sleep(.08)
                assert ro(app)['selected'] and len(ro(app)['handles'])==2,ro(app)
                press(app,b[0]+6,y,False)
                assert ro(app)['selected'] and len(ro(app)['handles'])==2,ro(app)
                screenshot(app,str(width)+'-anchor-handles')
                # The shared editor uses the same noncollapsing endpoint rule.
                app.tap(width-4,height//2)
                bounds_tap(app,app.diag()['composer_bounds'])
                ui.wait_for(lambda:editor(app)['active'])
                app.editor_action('official');request=app.receive('keyboard.open')
                app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='Alpha bravo')
                ui.wait_for(lambda:not editor(app)['active'])
                bounds_tap(app,app.diag()['composer_bounds']);ui.wait_for(lambda:editor(app)['active'])
                e=editor(app);b=e['text_bounds'];space=e['coordinate_space']
                def native(x,y):
                    if not space['scaled']:return x,y
                    return round(x*space['native_width']/space['width']),round(y*space['native_height']/space['height'])
                tx,ty=native(b[0]+20,b[1]+17)
                app.long_press(tx,ty,.52);assert editor(app)['selected']
                h=editor(app)['selection_handles'][-1]
                hx,hy=native(h['x'],h['y']);px,py=native(b[0]+6,b[1]+17)
                press(app,hx,hy);press(app,px,py);time.sleep(.08)
                assert editor(app)['selected'] and len(editor(app)['selection_handles'])==2,editor(app)
                press(app,px,py,False)
                assert editor(app)['selected'] and len(editor(app)['selection_handles'])==2,editor(app)
                screenshot(app,str(width)+'-editor-anchor-handles')
            print('Selection handles:',width,height,'code' if code else 'anchor','passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(960,266),(640,360),(568,1210)]:
        run(*size)
        run(*size,code=True)
