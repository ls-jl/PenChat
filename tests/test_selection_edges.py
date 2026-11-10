#!/usr/bin/env python3
"""Keep full-size clipped grips and reach line-final insertion boundaries on-screen."""
import pathlib
import sqlite3
import tempfile
import time
import test_lvgl_app as ui
from test_text_selection import BINARY, fixture, ro, press, screenshot, editor, bounds_tap


def drag(app,start,end):
    press(app,*start)
    press(app,*end);time.sleep(.06)
    press(app,*end,False);time.sleep(.06)


def run(width,height):
    first='Alpha bravo Z';second='Second line Q'
    with tempfile.TemporaryDirectory(prefix='penchat-selection-edge-') as tmp:
        folder=pathlib.Path(tmp);fixture(folder)
        with sqlite3.connect(folder/'penchat.db') as db:
            db.execute("UPDATE chat_messages SET output_text=? WHERE id='a'",(first+'\n\n'+second,))
        app=ui.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height,'direction':0})
        try:
            r=next(r for r in ro(app)['regions'] if r['key']=='a');p=r['parts'][0]
            app.long_press(p[0]+14,p[1]+12,.52);s=ro(app)
            sizes=[(g[2]-g[0],g[3]-g[1]) for g in s['handle_shapes']]
            assert sizes[0]==sizes[1] and sizes[0][0]>=18,s
            if height>width:assert s['handle_shapes'][0][0]<0,s
            gx,gy=s['handle_grips'][1]
            drag(app,(max(0,min(width-1,gx)),gy),(width-1,gy))
            assert ro(app)['end']==len(first),('first line final character',ro(app))
            shapes=ro(app)['handle_shapes']
            assert [(g[2]-g[0],g[3]-g[1]) for g in shapes]==sizes
            screenshot(app,str(width)+'-edge-fullsize-handles')
            # Dragging the clipped left disk from its visible portion still works.
            gx,gy=ro(app)['handle_grips'][0]
            drag(app,(max(1,gx),gy),(0,gy))
            assert ro(app)['start']==0 and ro(app)['end']==len(first),ro(app)
            gx,gy=ro(app)['handle_grips'][1]
            drag(app,(min(width-1,gx),gy),(width-1,gy+r['parts'][1][1]-p[1]))
            assert ro(app)['end']==r['length'],('message final character',ro(app))
            app.tap(width-4,height//2)
            bounds_tap(app,app.diag()['composer_bounds']);ui.wait_for(lambda:editor(app)['active'])
            app.editor_action('official');request=app.receive('keyboard.open')
            app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=first+'\n'+second)
            ui.wait_for(lambda:not editor(app)['active'])
            bounds_tap(app,app.diag()['composer_bounds']);ui.wait_for(lambda:editor(app)['active'])
            e=editor(app);b=e['text_bounds'];space=e['coordinate_space']
            sx=round(256*space['native_width']/space['width'])/256 if space['scaled'] else 1
            sy=round(256*space['native_height']/space['height'])/256 if space['scaled'] else 1
            def native(x,y):return round(x*sx),round(y*sy)
            app.long_press(*native(b[0]+14,b[1]+10),.52)
            e=editor(app);assert e['selected'],e
            g=next(g for g in e['selection_handles'] if g['endpoint']==1)
            gx,gy=native(*g['grip'])
            drag(app,(min(width-1,gx),gy),(width-1,gy))
            assert editor(app)['selection_end']==len(first),('editor line final character',editor(app))
            screenshot(app,str(width)+'-editor-edge-handles')
            print('Fixed-size edge grips and on-screen line-final selection:',width,height,'passed',flush=True)
        finally:app.close()


if __name__=='__main__':
    for size in [(960,266),(640,360),(568,1210)]:run(*size)
