#!/usr/bin/env python3
"""Completing a long draft away from bottom preserves the reader and explicit reveal."""
import pathlib,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed

t.port=1
for w,h in [(960,266),(936,280),(640,260)]:
 with tempfile.TemporaryDirectory(prefix='penchat-draft-anchor-') as folder:
  seed(folder,'\n\n'.join('阅读锚点 '+str(i) for i in range(120)))
  a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   for repeat in range(2):
    for i in range(9):a.send('touch',x=w//2,y=60+10*i,pressed=True);time.sleep(.025)
    a.send('touch',x=w//2,y=140,pressed=False);time.sleep(.25)
   t.wait_for(lambda:a.diag()['composer_hidden'])
   b=a.diag()['composer_shortcut']['bounds'];x=(b[0]+b[2])//2;y=(b[1]+b[3])//2
   a.tap(x,y);a.tap(x,y);t.wait_for(lambda:not a.diag()['composer_hidden'])
   before=a.diag();b=before['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   t.wait_for(lambda:a.diag()['editor']['active']);a.editor_action('official');r=a.receive('keyboard.open')
   a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text='第一行\n第二行\n第三行\n第四行')
   t.wait_for(lambda:not a.diag()['editor']['active']);time.sleep(.4);after=a.diag()
   assert not after['composer_hidden'] and not after['following_latest']
   assert abs(after['scroll_y']-before['scroll_y'])<=1,(before['scroll_y'],after['scroll_y'])
   assert after['composer_shortcut']['distance']==0 and after['draft_length']==15
   print('LONG_DRAFT_COMPLETION_PRESERVES_READER_ANCHOR_AND_REVEAL',w,h,flush=True)
  finally:a.close()
