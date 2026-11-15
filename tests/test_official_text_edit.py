#!/usr/bin/env python3
"""Native caret/selection owns official-IME editing without rebuilding its connection."""
import pathlib,tempfile,time
import test_lvgl_app as ui
ui.port=1
with tempfile.TemporaryDirectory(prefix='official-text-') as tmp:
 a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',pathlib.Path(tmp),official_adapter=False,screen={'width':568,'height':1210})
 try:
  b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
  a.editor_action('official');request=a.receive('keyboard.open')['requestId']
  def touch(x,y,down=True):
   a.send('keyboard.layout',requestId=request,height=590,foreground=True)
   a.send('touch',x=x,y=y,pressed=down);time.sleep(.04)
  def ack():
   event=a.receive('keyboard.edit');a.send('keyboard.edit.result',requestId=request,sequence=event['sequence'],ok=True,text=event['text']);a.diag();return event
  def action(name):
   b=a.diag()['editor']['actions'][name];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2;touch(x,y);touch(x,y,False);return ack()
  a.send('keyboard.layout',requestId=request,height=590,foreground=True)
  a.send('keyboard.change',requestId=request,text='alpha beta 中文😀',cursorIndex=15)
  b=a.diag()['editor']['text_bounds'];x,y=b[0]+5,(b[1]+b[3])//2
  touch(x,y);touch(x,y,False);event=ack();assert event['cursorIndex']==0,event
  time.sleep(.4);touch(x,y);touch(x,y,False);ack();assert a.diag()['editor']['menu']
  touch(x,y)
  for _ in range(14):touch(x,y)
  for i in range(1,8):touch(x+i*20,y)
  touch(x+140,y,False);event=ack();assert event['cursorIndex']>0
  touch(x+30,y)
  for _ in range(14):touch(x+30,y)
  touch(x+30,y,False);ack();assert a.diag()['editor']['selected'] and a.diag()['editor']['menu']
  action('all');assert a.diag()['editor']['selection_start']==0
  event=action('copy');assert a.diag()['editor']['clipboard_count']==1
  # Tap once to collapse selection, then hold to get Paste.
  touch(x,y);touch(x,y,False);ack()
  time.sleep(.4);touch(x,y);touch(x,y,False);ack()
  assert a.diag()['editor']['menu']
  event=action('paste');assert 'alpha beta 中文😀' in event['text'];assert a.diag()['keyboard']
  a.send('keyboard.result',requestId=request,confirmed=True,retained=True,text=event['text'])
  assert a.diag()['draft_length']==len(event['text'])
  a.open_settings();a.setting('models');a.page_action('添加 Provider');a.tap(220,110)
  ui.wait_for(lambda:a.diag()['editor']['active']);a.editor_action('official');request=a.receive('keyboard.open')['requestId']
  a.send('keyboard.layout',requestId=request,height=590,foreground=True)
  a.send('keyboard.change',requestId=request,text='Provider 示例',cursorIndex=11)
  b=a.diag()['editor']['text_bounds'];x,y=b[0]+5,(b[1]+b[3])//2
  touch(x,y);touch(x,y,False);assert ack()['cursorIndex']==0
  time.sleep(.4);touch(x,y);touch(x,y,False);ack();assert a.diag()['editor']['menu']
  assert a.diag()['view']=='provider' and a.diag()['keyboard']
  print('OFFICIAL_NATIVE_CARET_DRAG_SELECTION_COPY_PASTE_RETAIN_OK')
 finally:a.close()
