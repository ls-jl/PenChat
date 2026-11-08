#!/usr/bin/env python3
"""Composer tap remains an editor; composer pull shares guarded chat creation."""
import os,pathlib,tempfile,time
from test_lvgl_app import Native,ROOT
from test_chat_style import seed,drag
import test_lvgl_app as transport
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
for content in [False,True]:
 with tempfile.TemporaryDirectory() as directory:
  if content:seed(directory,'测试会话')
  app=Native(binary,directory,official_adapter=False)
  try:
   first=app.diag()['session']
   drag(app,[232,212,180,150,120,66],False)
   state=app.diag()
   assert not state['editor']['active']
   if content:
    assert state['pull_progress']==99 and state['pull_full_distance']==167
    assert state['pull_reader_height']==208
    drag(app,[140]);assert app.diag()['session']==first
    drag(app,[232,210,180,140,110,65],False)
    assert app.diag()['pull_progress']==100
    app.send('touch',x=500,y=90,pressed=False,cancelled=True);time.sleep(.2)
    assert app.diag()['session']==first
    drag(app,[232,210,180,140,110,65])
    assert app.diag()['session']!=first
   else:
    assert not state['pull_active']
    drag(app,[90]);assert app.diag()['session']==first
   assert not app.diag()['editor']['active']
   time.sleep(.25);app.tap(500,232);assert app.diag()['editor']['active']
  finally:app.close()
print('Composer pull: empty guard, new baseline boundary, retreat/cancel, creation and tap passed')

# With conversation content, a long draft scrolls locally before a fresh pull
# may create a new chat. A single gesture cannot switch owners at its bottom.
for w,h in [(960,266),(936,280)]:
 with tempfile.TemporaryDirectory() as directory:
  seed(directory,'已有会话')
  app=Native(binary,directory,official_adapter=False,screen={'width':w,'height':h})
  try:
   d=app.diag();r=d['composer_text_bounds'];app.tap((r[0]+r[2])//2,(r[1]+r[3])//2)
   app.editor_action('official');request=app.receive('keyboard.open')
   text='长草稿滚动测试。'*180
   app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=text)
   transport.wait_for(lambda:not app.diag()['editor']['active']);first=app.diag()['session']
   count=0
   while app.diag()['composer_scroll_bottom']>0:
    d=app.diag();r=d['composer_text_bounds'];x=(r[0]+r[2])//2;y=r[3]-6
    before=d['composer_scroll']
    for i in range(8):app.send('touch',x=x,y=y-i*6,pressed=True);time.sleep(.025)
    d=app.diag();assert not d['pull_active'] and d['session']==first
    # Hold and move past the local bottom. Ownership must remain local.
    if d['composer_scroll_bottom']<=0:
     app.send('touch',x=x,y=8,pressed=True);assert not app.diag()['pull_active']
    app.send('touch',x=x,y=y-42,pressed=False);time.sleep(.15)
    d=app.diag();assert d['composer_scroll']>before and d['draft_length']==len(text)
    count+=1;assert count<100
   # Start a separate deliberate pull only after the local text reached bottom.
   r=app.diag()['composer_text_bounds'];x=(r[0]+r[2])//2;y=r[3]-6
   full=app.diag()['pull_full_distance']
   for i in range(11):app.send('touch',x=x,y=y-int((full+20)*i/10),pressed=True);time.sleep(.06)
   d=app.diag();assert d['pull_active'] and d['pull_progress']==100
   app.send('touch',x=x,y=y-full-20,pressed=False,cancelled=True)
   time.sleep(.2);assert app.diag()['session']==first and app.diag()['draft_length']==len(text)
  finally:app.close()
print('Landscape long drafts: local scrolling, no mid-touch pull takeover and fresh pull/cancel at text bottom passed')
