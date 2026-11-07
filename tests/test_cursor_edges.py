#!/usr/bin/env python3
"""Insertion hit testing stays independent from the floating magnifier bounds."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/cursor-edges';OUT.mkdir(parents=True,exist_ok=True)
for width,height in [(960,266),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-cursor-edges-') as folder:
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height})
        try:
            def e():return a.diag()['editor']
            def open_input():
                b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
                t.wait_for(lambda:e()['active'] and bool(e()['keys']))
            open_input()
            for case,text in [('latin','ABCDE'),('chinese','左右边界'),('emoji','ABC👩‍💻'),('multiline','first line\nsecondM'),
                              ('wrap','M'*90),('wrap-cn','中文输入填满这一行并检查右侧字符边界ABC下一行继续输入。'*3)]:
                a.editor_action('official');request=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=text)
                t.wait_for(lambda:not e()['active']);open_input()
                current=e();bounds=current['text_bounds']
                # Locate the line end from a far-right captured drag first.
                x,y=bounds[0]+6,bounds[1]+6
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.52)
                if case.startswith('wrap'):
                    a.send('touch',x=-80,y=y,pressed=True);time.sleep(.06)
                    first=e()['caret'][1]
                    a.send('touch',x=width+80,y=y,pressed=True);time.sleep(.06)
                    assert 0<e()['cursor']<len(text) and e()['caret'][1]>first
                    line_end=e()['cursor']
                    # Full rows must reach the same insertion boundary from
                    # the last visible field pixel, not only far outside it.
                    a.send('touch',x=bounds[2],y=y,pressed=True);time.sleep(.06)
                    assert e()['cursor']==line_end,(width,case,'filled row edge',e()['cursor'],line_end)
                a.send('touch',x=width+80,y=bounds[3]-2,pressed=True);time.sleep(.08)
                assert e()['cursor']==len(text),(width,case,'far-right',e()['cursor'],len(text))
                end=e()['caret'];y=end[1]-16
                a.send('touch',x=end[0]-1,y=y,pressed=True);time.sleep(.08)
                assert e()['cursor']==len(text),(width,case,'last glyph right half',e()['cursor'],len(text))
                current=e();bubble=current['magnifier_bounds'];bounds=current['text_bounds']
                assert bounds[0]<=bubble[0]<=bubble[2]<=bounds[2],(bubble,bounds)
                capture(a,OUT/f'{width}-{case}-right.png')
                a.send('touch',x=-80,y=bounds[1]+5,pressed=True);time.sleep(.08)
                assert e()['cursor']==0
                bubble=e()['magnifier_bounds'];assert bounds[0]<=bubble[0]<=bubble[2]<=bounds[2]
                a.send('touch',x=-80,y=bounds[1]+5,pressed=False)
                assert not e()['cursor_drag'] and not e()['magnifier']
            print(f'Cursor {width}x{height}: last glyph insertion, captured outside drag, multiline/emoji and independent lens bounds passed.',flush=True)
        finally:a.close()
