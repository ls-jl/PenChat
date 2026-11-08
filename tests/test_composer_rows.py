#!/usr/bin/env python3
"""Landscape grows in-place to 2x; portrait keeps full-width text above tools."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/composer-rows';OUT.mkdir(parents=True,exist_ok=True)
for width,height in [(960,266),(936,280),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-composer-rows-') as folder:
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height})
        try:
            def e():return a.diag()['editor']
            def open_input():
                d=a.diag();r=d['composer_text_bounds'];a.tap((r[0]+r[2])//2,(r[1]+r[3])//2)
                t.wait_for(lambda:e()['active'] and bool(e()['keys']))
            def key(value):
                k=next(k for k in e()['keys'] if k['key']==value);a.tap(k['x'],k['y'])
            d=a.diag();b=d['composer_bounds'];base=b[3]-b[1]+1
            assert base==(94 if height>width else 48)
            assert d['send_center_y2']==d['capsule_center_y2'];capture(a,OUT/f'{width}-empty.png')
            open_input()
            if height>width:
                d=a.diag();b=d['composer_bounds'];r=e()['text_bounds'];send=e()['actions']['chat-send']
                assert b[3]-b[1]+1>base and r[3]<send[1]
                assert r[0]-b[0]==12 and b[2]-r[2]==12
                assert b[2]-send[2]==12 and b[3]-send[3]==12
                capture(a,OUT/f'{width}-empty-keyboard.png')
                key('language');key('a');heights=[]
                for i in range(10):
                    key('enter');key('a');d=a.diag();b=d['composer_bounds'];r=e()['text_bounds'];send=e()['actions']['chat-send']
                    heights.append(b[3]-b[1]+1);assert r[3]<send[1] and b[2]-send[2]==12 and b[3]-send[3]==12
                assert heights==sorted(heights) and heights[-1]==3*base and e()['text_scroll']>0
                capture(a,OUT/f'{width}-long-keyboard.png');a.editor_action('done')
            else:
                a.editor_action('official');req=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text='短草稿')
            t.wait_for(lambda:not e()['active']);d=a.diag();b=d['composer_bounds'];r=d['composer_text_bounds']
            if height>width:
                assert b[3]<height and b[3]-b[1]+1>base and r[3]<d['send_center_y2']//2-21
                assert r[0]-b[0]==12 and b[2]-r[2]==12
                assert b[3]-d['send_center_y2']//2-22 in (11,12)
            else:
                assert b[3]-b[1]+1==base and r[0]>b[0]+44
                assert d['send_center_y2']==d['capsule_center_y2']
                open_input();a.editor_action('official');req=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text='第一行\n第二行')
                t.wait_for(lambda:not e()['active']);d=a.diag();b=d['composer_bounds']
                assert base<b[3]-b[1]+1<=base*2 and d['composer_radius']==base//2
                capture(a,OUT/f'{width}-two-lines.png')
                open_input();a.editor_action('official');req=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text='Long draft with words and 中文分段。'*35)
                t.wait_for(lambda:not e()['active']);d=a.diag();b=d['composer_bounds'];r=d['composer_text_bounds']
                assert b[3]-b[1]+1==base*2 and r[0]>b[0]+44 and r[2]<b[2]-44
                assert d['composer_radius']==base//2
                # The text has a bounded local scroller, independent of chat.
                x=(r[0]+r[2])//2;y=r[3]-4
                for step in range(8):a.send('touch',x=x,y=y-step*6,pressed=True);time.sleep(.025)
                a.send('touch',x=x,y=y-42,pressed=False);time.sleep(.2)
                assert a.diag()['composer_scroll']>0 and not e()['active']
            capture(a,OUT/f'{width}-draft.png')
            # Blank toolbar space still focuses the composer, without stealing
            # the attachment or send controls. Editing never directly sends.
            open_input()
            a.editor_action('official');req=a.receive('keyboard.open');a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text='')
            t.wait_for(lambda:not e()['active']);d=a.diag();b=d['composer_bounds']
            assert b[3]-b[1]+1==base and not a.read_messages()
            print(f'Composer {width}x{height}: orientation-specific rows, bounded growth/scroll, fixed landscape corner radius and entry focus passed.',flush=True)
        finally:a.close()
