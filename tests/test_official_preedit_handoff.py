#!/usr/bin/env python3
"""Fullscreen Cancel restores the draft; live docked dismissal retains committed text."""
import os,pathlib,tempfile
import test_lvgl_app as ui
ui.port=1
for w,h in [(960,266),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-official-preedit-probe-') as folder:
  a=ui.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl')),folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);ui.wait_for(lambda:a.diag()['editor']['keys'])
   def key(name):
    e=a.diag()['editor'];k=next(k for k in e['keys'] if k['key']==name);space=e['coordinate_space'];a.tap(k['x']*w/space['width'],k['y']*h/space['height'])
   if not a.diag()['editor']['chinese']:key('language')
   for c in 'ni':key(c)
   ui.wait_for(lambda:a.diag()['editor']['preedit_length']==2)
   a.editor_action('official');r=a.receive('keyboard.open');a.send('keyboard.result',requestId=r['requestId'],confirmed=False)
   ui.wait_for(lambda:not a.diag()['keyboard']);s=a.diag()
   print(w,h,'seed_length',len(r['text']),'draft_length',s['draft_length'],'editor_active',s['editor']['active'],flush=True)
   assert s['draft_length']==(len(r['text']) if r['inline'] else 0) and not s['editor']['active']
   b=s['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);ui.wait_for(lambda:a.diag()['editor']['keys'])
   a.editor_action('official');request=a.receive('keyboard.open')
   a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='')
   ui.wait_for(lambda:not a.diag()['keyboard']);assert a.diag()['draft_length']==0
   assert not a.read_messages()
  finally:a.close()
