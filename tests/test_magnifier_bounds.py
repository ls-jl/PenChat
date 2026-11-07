#!/usr/bin/env python3
"""Render and drag the real native magnifier against logical display safe edges."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/magnifier-bounds';OUT.mkdir(parents=True,exist_ok=True)
for width,height in [(960,266),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-magnifier-bounds-') as folder:
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height})
        try:
            def editor():return a.diag()['editor']
            def open_input():
                s=a.diag();a.tap(250 if height>width else 500,s['toolbar_y']+30 if height>width else 232)
                t.wait_for(lambda:editor()['active'] and bool(editor()['keys']))
            open_input();a.editor_action('official');r=a.receive('keyboard.open')
            a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text='Edge one: abcdefghijklmnopqrstuvwxyz\n'+'More text for cursor dragging.\n'*12)
            t.wait_for(lambda:not editor()['active']);open_input()
            bounds=editor()['text_bounds'];x,y=bounds[0]+18,bounds[1]+18
            a.send('touch',x=x,y=y,pressed=True);time.sleep(.52)
            a.send('touch',x=x+20,y=y,pressed=True);t.wait_for(lambda:editor()['magnifier'])
            for name,x,y in [('top-left',0,0),('top-right',width-1,0),('bottom-right',width-1,height-1),('bottom-left',0,height-1)]:
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.12)
                s=a.diag();e=s['editor'];assert e['cursor_drag'] and e['magnifier']
                left,top,right,bottom=e['magnifier_bounds'];safe=s['safe_insets'] if height>width else [4,4,4,10]
                assert left>=safe[0] and top>=safe[1] and right<width-safe[2] and bottom<height-safe[3],(name,e['magnifier_bounds'],safe)
                c=e['magnifier_caret'];assert left<=c[0]<=c[2]<=right and top<=c[1]<=c[3]<=bottom,(name,c,e['magnifier_bounds'])
                assert right-left+1==180 and bottom-top+1==58
                assert abs((c[0]+c[2]+1)//2-(left+right+1)//2)<=1
                capture(a,OUT/f'{width}-{name}.png')
            a.send('touch',x=x,y=y,pressed=False,cancelled=True)
            t.wait_for(lambda:not editor()['magnifier']);a.editor_action('done' if height>width else 'cancel')
            if height>width:
                # A real form field near the top is the Y07 rounded-corner case:
                # preserve the whole bubble even though the raised offset cannot fit.
                a.open_settings();a.setting('models');a.page_action('添加 Provider');a.tap(220,110)
                t.wait_for(lambda:editor()['active'] and editor()['text_in_target'])
                b=editor()['text_bounds'];x,y=b[0]+12,b[1]+16
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.52)
                a.send('touch',x=x+20,y=y,pressed=True);t.wait_for(lambda:editor()['magnifier'])
                a.send('touch',x=0,y=0,pressed=True);time.sleep(.1)
                e=editor();assert e['magnifier_bounds'][:2]==[24,24],e['magnifier_bounds']
                capture(a,OUT/f'{width}-top-field.png')
                a.send('touch',x=0,y=0,pressed=False,cancelled=True);a.editor_action('cancel')
            print(f'Magnifier {width}x{height}: four-edge drag, safe bounds, centered lens/caret and cancel passed.',flush=True)
        finally:a.close()
