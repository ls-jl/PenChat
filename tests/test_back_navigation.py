#!/usr/bin/env python3
"""Native edge-back arbitration, including root exit, without Falcon gestures."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from test_image_viewer import fixture,open_photo,shot
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))

def swipe(a,points,cancel=False,hold=False):
    for x,y in points:a.send('touch',x=x,y=y,pressed=True);time.sleep(.035)
    if not hold:
        x,y=points[-1];a.send('touch',x=x,y=y,pressed=False,cancelled=cancel);time.sleep(.10)

def action(a,title):
    x1,y1,x2,y2=a.diag()['page']['actions'][title]
    a.tap((x1+x2)//2,(y1+y2)//2)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-back-') as directory:
        p=pathlib.Path(directory);fixture(p)
        a=t.Native(BINARY,p,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            assert a.receive('navigation.state')['root']
            a.open_settings();assert not a.diag()['back_navigation']['root']
            a.setting('models');action(a,'添加 Provider')
            swipe(a,[(20,120),(60,122),(160,124)])
            assert a.diag()['view']=='provider' and '添加 Provider' in a.diag()['page']['actions']
            swipe(a,[(20,120),(60,122),(160,124)])
            assert a.diag()['view']=='settings'
            # Falcon completions cannot revive a cancelled native gesture.
            seq=a.diag()['back_navigation']['sequence']
            a.send('touch',x=20,y=120,pressed=True,gesture=701)
            a.send('touch',x=0,y=0,pressed=False,cancelled=True,gesture=701)
            a.send('navigation.back',gesture=701,sequence=seq)
            assert a.diag()['view']=='settings'
            # Claimed-but-short, retreat, cancelled, vertical and interior drags
            # must never dispatch a return or a delayed button click.
            for points,cancel in [([(20,120),(70,120)],False),
                                  ([(20,120),(160,120),(25,120)],False),
                                  ([(20,120),(160,120)],True),
                                  ([(20,120),(22,70)],False),
                                  ([(300,30),(480,30)],False)]:
                swipe(a,points,cancel);assert a.diag()['view']=='settings',(w,points,cancel,a.diag()['view'])
            swipe(a,[(20,120),(80,120),(160,120)],hold=True)
            assert a.diag()['back_navigation']['claimed'];shot(a,f'{w}-back-progress')
            a.send('visibility',visible=False);a.send('visibility',visible=True)
            a.send('touch',x=160,y=120,pressed=False);assert a.diag()['view']=='settings'
            a.tap(40,32);assert a.diag()['navigation_open']
            swipe(a,[(w-160,80),(w-40,80)])
            assert a.diag()['navigation_open'] and not a.diag()['back_navigation']['root']
            a.tap(w-100,80);assert a.diag()['back_navigation']['root']
            # Image panning keeps interior gestures; edge-back closes only the
            # viewer and retains its session and reading position.
            before=open_photo(a);t.wait_for(lambda:a.diag()['image_viewer']['loaded'])
            swipe(a,[(300,120),(480,120)]);assert a.diag()['image_viewer']['open']
            swipe(a,[(20,120),(80,120),(160,120)])
            s=a.diag();assert not s['image_viewer']['open'] and s['session']==before['session'] and s['scroll_y']==before['scroll_y']
            if s['to_bottom_visible']:a.tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
            t.wait_for(lambda:a.diag()['toolbar_y']==h-58)
            # Normal edge-key taps survive arbitration. Right-swiping never
            # submits that key, nor does canceling the editor apply its text.
            a.tap(w//2,h-34);t.wait_for(lambda:a.diag()['editor']['active'])
            q=next(k for k in a.diag()['editor']['keys'] if k['key']=='q');a.tap(q['x'],q['y'])
            assert a.diag()['editor']['preedit_length']==1
            swipe(a,[(20,120),(70,120)]);assert a.diag()['editor']['active']
            swipe(a,[(20,120),(80,120),(160,120)])
            assert not a.diag()['editor']['active'] and a.diag()['draft_length']==0
            a.open_settings();a.setting('reasoning');assert a.diag()['modal']
            swipe(a,[(20,120),(80,120),(160,120)])
            assert not a.diag()['modal'] and a.diag()['view']=='settings'
            a.to_chat();a.tap(128,h-34);assert a.diag()['attachments']
            swipe(a,[(20,120),(80,120),(160,120)])
            assert not a.diag()['attachments'] and a.diag()['back_navigation']['root']
            swipe(a,[(20,120),(80,120),(160,120)])
            assert a.receive('finish')['type']=='finish'
        finally:a.close()

if __name__=='__main__':
    for size in ((960,266),(936,280)):run(*size)
    print('Native back: nested pages, overlays, editor cancel, tap replay, scroll/pan exclusion, retreat, interruption and drawer tap-only passed at both sizes.')
