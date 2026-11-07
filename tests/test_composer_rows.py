#!/usr/bin/env python3
"""Single collapsed row; full-width text above attachment/gauge/send controls."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/composer-rows';OUT.mkdir(parents=True,exist_ok=True)
for width,height in [(960,266),(568,1210)]:
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
                a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text='第一行独占整行文字，附件与发送保留在下面一行。')
            t.wait_for(lambda:not e()['active']);d=a.diag();b=d['composer_bounds'];r=d['composer_text_bounds']
            assert b[3]<height and b[3]-b[1]+1>base and r[3]<d['send_center_y2']//2-21
            assert r[0]<b[0]+40 and r[2]>b[2]-40
            assert r[0]-b[0]==12 and b[2]-r[2]==12
            assert b[3]-d['send_center_y2']//2-22 in (11,12)
            capture(a,OUT/f'{width}-draft.png')
            # Blank toolbar space still focuses the composer, without stealing
            # the attachment or send controls. Editing never directly sends.
            a.tap((b[0]+b[2])//2,d['send_center_y2']//2);t.wait_for(lambda:e()['active'])
            a.editor_action('official');req=a.receive('keyboard.open');a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text='')
            t.wait_for(lambda:not e()['active']);d=a.diag();b=d['composer_bounds']
            assert b[3]-b[1]+1==base and not a.read_messages()
            print(f'Composer {width}x{height}: empty collapse, separate text/actions, full width, growth/scroll and blank toolbar focus passed.',flush=True)
        finally:a.close()
