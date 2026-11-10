#!/usr/bin/env python3
"""The portrait globe has a generous invisible target without stealing punctuation."""
import pathlib,tempfile,time
import test_lvgl_app as ui
from test_pinyin_ui import BINARY,open_editor,editor,key
from image_gestures import capture
ui.port=1
OUT=ui.ROOT/'.work/portrait-globe-hit';OUT.mkdir(parents=True,exist_ok=True)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-globe-hit-') as tmp:
        app=ui.Native(BINARY,pathlib.Path(tmp),official_adapter=False,screen={'width':w,'height':h})
        try:
            open_editor(app);g=next(k for k in editor(app)['keys'] if k['key']=='language')
            b=g['bounds'];hit=g['hit_bounds']
            assert b[2]-b[0]==48 and b[3]-b[1]==48
            assert hit[2]-hit[0]==96 and hit[3]-hit[1]>=72,(b,hit)
            for k in editor(app)['keys']:
                if k['key']=='language':continue
                r=k['hit_bounds']
                assert r[2]<=hit[0] or r[0]>=hit[2] or r[3]<=hit[1] or r[1]>=hit[3],(k,hit)
            left=(hit[0]+2,(hit[1]+hit[3])//2);right=(hit[2]-2,left[1])
            assert left[0]<b[0] and right[0]>b[2]
            app.tap(*left);assert editor(app)['layout']=='en26'
            app.tap(*right);assert editor(app)['layout']=='zh9'
            # Exercise the actual native-contact path outside the visible icon.
            app.send('touch',x=left[0],y=left[1],pressed=True,points=[{'id':'globe','x':left[0],'y':left[1]}])
            time.sleep(.56);assert editor(app)['layout_menu']
            app.send('touch',x=left[0],y=left[1],pressed=False,points=[])
            assert editor(app)['layout_menu'] and editor(app)['layout']=='zh9'
            app.editor_action('layout:0');ui.wait_for(lambda:not editor(app)['layout_menu'])
            assert next(k for k in editor(app)['keys'] if k['key']=='language')['bounds']==b
            comma=next(k for k in editor(app)['keys'] if k['key']==',')
            app.tap(comma['x'],comma['bounds'][3]-1)
            assert editor(app)['length']==1 and editor(app)['layout']=='zh26'
            capture(app,OUT/f'{w}-globe-unchanged.png')
            print('Expanded globe target, edge taps, held native contact and punctuation isolation:',w,h,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(568,1210),(600,1280)]:run(*size)
