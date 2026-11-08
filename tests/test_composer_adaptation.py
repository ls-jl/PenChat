#!/usr/bin/env python3
"""Bound docked forms and keep directional composer motion separate from following."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/composer-adaptation';OUT.mkdir(parents=True,exist_ok=True)
def swipe(a,x,y,dy):
    for i in range(9):a.send('touch',x=x,y=y+dy*i//8,pressed=True);time.sleep(.025)
    a.send('touch',x=x,y=y+dy,pressed=False);time.sleep(.25)
def open_input(a):
    b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    t.wait_for(lambda:a.diag()['editor']['active'] and bool(a.diag()['editor']['keys']))
for w,h in [(960,266),(936,280),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-composer-adapt-') as folder:
        seed(folder,'\n\n'.join('阅读方向保持稳定。%d'%i for i in range(60)))
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h})
        try:
            open_input(a);a.editor_action('official');request=a.receive('keyboard.open')
            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='保留这份草稿')
            t.wait_for(lambda:not a.diag()['editor']['active'])
            # Move away from bottom without returning there during assertions.
            swipe(a,w//2,90,100 if h<w else 280)
            assert not a.diag()['following_latest']
            swipe(a,w//2,155 if h<w else 450,-80 if h<w else -180)
            d=a.diag();assert d['composer_hidden'] and d['toolbar_y']>=h
            capture(a,OUT/f'{w}-hidden.png')
            swipe(a,w//2,70,70 if h<w else 200)
            d=a.diag();assert not d['composer_hidden'] and d['composer_bounds'][3]<h and not d['following_latest']
            capture(a,OUT/f'{w}-draft-revealed.png')
            if h>w:
                open_input(a);assert a.diag()['editor']['docked']
                swipe(a,w//2,350,-180)
                assert a.diag()['composer_hidden'] and a.diag()['editor']['active']
                swipe(a,w//2,160,180)
                assert not a.diag()['composer_hidden'] and a.diag()['editor']['active']
                a.editor_action('done');a.open_settings();a.setting('models');a.page_action('添加 Provider')
                targets=a.diag()['editor']['targets'];b=next(b for k,b in targets.items() if k.startswith('field:名称:'))
                a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
                t.wait_for(lambda:a.diag()['editor']['active'])
                for _ in range(3):swipe(a,w//2,500,-360)
                d=a.diag();assert d['scroll_y']+d['scroll_bottom']<120,d
                assert d['page_content_height']<d['reader_height']+120
                globe=next(k for k in d['editor']['keys'] if k['key']=='language')
                comma=next(k for k in d['editor']['keys'] if k['key']==',')
                assert abs(globe['y']-(comma['bounds'][3]+h-1)/2)<=1
                capture(a,OUT/'portrait-form-bottom.png')
                a.editor_action('done');d=a.diag()
                assert d['reader_height']==h and d['scroll_y']+d['scroll_bottom']<=0
            print(f'{w}x{h}: draft hides upward/reveals downward without changing follow; docked viewport has no stale full-screen spacer.',flush=True)
        finally:a.close()
