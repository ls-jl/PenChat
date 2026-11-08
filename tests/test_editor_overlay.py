#!/usr/bin/env python3
"""Portrait clipboard replaces keys, expanded dismiss target, official failure recovery."""
import os,pathlib,tempfile
import test_lvgl_app as t
from image_gestures import capture
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/editor-overlay';OUT.mkdir(parents=True,exist_ok=True)
t.port=1
for w,h in [(568,1210),(600,1280),(960,266)]:
 with tempfile.TemporaryDirectory(prefix='penchat-overlay-') as folder:
  a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   def e():return a.diag()['editor']
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   t.wait_for(lambda:e()['active'] and bool(e()['keys']))
   globe=next(k for k in e()['keys'] if k['key']=='language');a.tap(globe['x'],globe['y'])
   a.editor_action('clipboard');assert e()['clipboard_open']
   close=e()['actions']['clip-close']
   if h>w:
    assert close[1]>=e()['panel_top']
    q=next(k for k in e()['keys'] if k['key']=='q')
    a.tap(q['x'],q['y']);assert e()['length']==0 and e()['clipboard_open']
    # Blank keyboard cover also absorbs taps on the old globe and close key.
    globe=next(k for k in e()['keys'] if k['key']=='language')
    a.tap(globe['x'],globe['y']);assert e()['layout']=='en26' and e()['clipboard_open']
   else:
    q=next(k for k in e()['keys'] if k['key']=='q')
    a.tap(q['x'],q['y']);assert e()['length']==1 # split keyboard remains usable
   capture(a,OUT/f'{w}-clipboard.png')
   a.tap((close[0]+close[2])//2,(close[1]+close[3])//2)
   assert not e()['clipboard_open']
   if h>w:
    done=e()['actions']['done']
    a.tap(done[0]-6,(done[1]+done[3])//2)
    t.wait_for(lambda:not e()['active'])
    b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    t.wait_for(lambda:e()['active'])
   if h>w:
    a.editor_action('official');request=a.receive('keyboard.open')
    assert request['field']['width']>0
    a.send('keyboard.layout',requestId=request['requestId'],height=h//2,foreground=True)
    t.wait_for(lambda:a.diag()['display_top_height']==h-h//2)
    assert a.diag()['display_visible'] and a.diag()['view']=='chat'
    assert a.diag()['composer_bounds'][3]<h-h//2
    a.send('keyboard.change',requestId=request['requestId'],text='中😀文',cursorIndex=3)
    t.wait_for(lambda:e()['length']==3)
    assert e()['cursor']==2
    capture(a,OUT/f'{w}-official-in-place.png')
    a.send('keyboard.layout',requestId=request['requestId'],height=h//2,foreground=False)
    assert not a.diag()['display_visible']
    a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
    t.wait_for(lambda:not a.diag()['keyboard'])
    assert e()['length']==0 and a.diag()['display_top_height']==0
   a.editor_action('official');request=a.receive('keyboard.open')
   a.send('keyboard.result',requestId='stale',confirmed=False,error='stale')
   assert a.diag()['keyboard']
   a.send('keyboard.result',requestId=request['requestId'],confirmed=False,error='官方键盘未能显示，已恢复内置键盘')
   t.wait_for(lambda:not a.diag()['keyboard'])
   assert e()['active'] and e()['notice_present']
   assert a.diag()['display_visible']
   capture(a,OUT/f'{w}-restored.png')
   print(f'{w}x{h}: clipboard placement/input isolation, dismiss hit target and official failure recovery passed.',flush=True)
  finally:a.close()
