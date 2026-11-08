#!/usr/bin/env python3
"""Portrait reader-to-keyboard dismissal, rollback and anchored attachment menus."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/docked-dismiss';OUT.mkdir(parents=True,exist_ok=True)
for w,h in [(568,1210),(600,1280)]:
 with tempfile.TemporaryDirectory(prefix='penchat-docked-dismiss-') as folder:
  seed(folder,'\n\n'.join('阅读页面，保留第%d段。'%i for i in range(60)))
  a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':180,'tp_direction':180})
  try:
   def e():return a.diag()['editor']
   def touch(y,down=True,cancel=False,x=None,points=None):
    x=w//2 if x is None else x
    p=points if points is not None else ([{'id':8,'x':x,'y':y}] if down else [])
    a.send('touch',x=x,y=y,pressed=down,cancelled=cancel,points=p);time.sleep(.035)
   def open_input():
    b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    t.wait_for(lambda:e()['active'] and bool(e()['keys']))
   def start_push(offset):
    edge=e()['panel_top']
    for y in [140,240,edge-40,edge,edge+offset]:touch(y)
    return edge
   open_input();keyboard=e()['keyboard_session']
   # A drag originating on a key remains a key gesture, never page dismissal.
   k=next(k for k in e()['keys'] if k['key']=='q')
   touch(k['y'],x=k['x']);touch(h-40,x=k['x']);touch(h-40,False,x=k['x'])
   assert e()['active'] and e()['dismiss_offset']==0 and e()['preedit_length']==0
   # Page swipes above the panel never hide the portrait composer.
   for y in [350,300,250,180]:touch(y)
   touch(180,False);assert not a.diag()['composer_hidden'] and e()['active']
   edge=start_push(45);assert e()['dismissing'] and e()['dismiss_offset']==45 and e()['active']
   assert a.diag()['composer_bounds'][3]>=edge+40
   assert e()['preedit_length']==0 and e()['length']==0
   capture(a,OUT/f'{w}-partial-push.png')
   touch(edge+45,False);t.wait_for(lambda:e()['dismiss_offset']==0 and not e()['dismiss_returning'])
   assert e()['active'] and e()['keyboard_session']==keyboard
   # Crossing the threshold then retreating cancels. So does touch cancellation.
   threshold=e()['dismiss_threshold'];edge=start_push(threshold+20)
   touch(edge+12);touch(edge+12,False);t.wait_for(lambda:e()['dismiss_offset']==0)
   assert e()['active'] and e()['keyboard_session']==keyboard
   edge=start_push(threshold+20);touch(edge+threshold+20,False,True)
   t.wait_for(lambda:e()['dismiss_offset']==0);assert e()['active']
   # An explicit second finger cancels instead of typing a key or dismissing.
   edge=start_push(threshold+20)
   touch(edge+threshold+20,points=[{'id':8,'x':w//2,'y':edge+threshold+20},{'id':9,'x':w//2+80,'y':edge+threshold+20}])
   touch(edge+threshold+20,False);t.wait_for(lambda:e()['dismiss_offset']==0);assert e()['active']
   # Background interruption restores the panel and preserves its edit session.
   edge=start_push(45);a.send('visibility',visible=False);assert e()['dismiss_offset']==0
   a.send('visibility',visible=True);t.wait_for(lambda:e()['active'] and bool(e()['keys']))
   assert e()['keyboard_session']==keyboard
   # Dismissal preserves unselected letters without choosing a candidate or sending.
   for letter in 'nihao':
    k=next(k for k in e()['keys'] if k['key']==letter);a.tap(k['x'],k['y'])
   t.wait_for(lambda:e()['candidate_count']>0)
   before=a.read_messages();edge=start_push(threshold+20)
   assert e()['active'];capture(a,OUT/f'{w}-full-push.png')
   touch(edge+threshold+20,False);t.wait_for(lambda:not e()['active'])
   assert a.diag()['draft_length']==5 and a.read_messages()==before
   # The plus menu grows upward from the current composer's top, capped to five rows.
   for state in ['closed-keyboard','open-keyboard']:
    if state=='open-keyboard':open_input()
    b=a.diag()['composer_bounds'];a.tap(b[0]+34,b[3]-34)
    t.wait_for(lambda:a.diag()['attachments'] and not e()['active'])
    t.wait_for(lambda:a.diag()['attachment_viewport'][3]-a.diag()['attachment_viewport'][1]+1==228)
    d=a.diag();v=d['attachment_viewport'];assert v[1]>h//3 and v[3]==d['composer_bounds'][1]-5,(v,d['composer_bounds'])
    capture(a,OUT/f'{w}-attachments-{state}.png')
    if state=='closed-keyboard':
     a.attachment_action('session_info');t.wait_for(lambda:a.diag()['view']=='session_info')
     a.to_chat()
    else:a.tap(w-12,100)
    assert not a.diag()['attachments'] and a.diag()['draft_length']==5
   # A form uses the same page-origin gesture, committing only its in-memory field.
   a.open_settings();a.setting('models');a.page_action('添加 Provider')
   b=next(b for k,b in e()['targets'].items() if k.startswith('field:名称:'))
   a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);t.wait_for(lambda:e()['active'])
   edge=start_push(threshold+20);touch(edge+threshold+20,False)
   t.wait_for(lambda:not e()['active']);assert a.diag()['view']=='provider'
   print(f'{w}x{h}: stable portrait composer, interactive push, retreat/cancel/multitouch/background, raw preedit and bounded upward attachment menu passed.',flush=True)
  except BaseException:
   capture(a,OUT/f'{w}-failure.png');raise
  finally:a.close()
