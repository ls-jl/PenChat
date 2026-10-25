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
