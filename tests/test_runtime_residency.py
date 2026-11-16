#!/usr/bin/env python3
"""Real LVGL editor survives detach/reattach without losing composition/objects."""
import os,pathlib,tempfile,time
import test_lvgl_app as ui
ui.port=1
for w,h in [(960,266),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-runtime-attach-') as folder:
  a=ui.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl')),folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   ui.wait_for(lambda:bool(a.diag()['editor']['keys']))
   for letter in 'ni':
    k=next(v for v in a.diag()['editor']['keys'] if v['key']==letter);a.tap(k['x'],k['y'])
   before=a.diag();a.send('page.detach');ui.wait_for(lambda:not a.diag()['display_visible'])
   assert a.process.poll() is None
   a.generation+=1;a.send('init',reattach=True,ocrCapabilities={'single':True,'multi':True})
   boot=a.receive('booted');assert boot['screen']['width']==w
   a.send('window.focus',focused=True,sequence=1);a.send('input.focus',active=True,sequence=1);a.receive('ready')
   after=a.diag();e=after['editor'];old=before['editor']
   assert e['session']==old['session'] and e['keyboard_session']==old['keyboard_session']
   assert e['preedit_length']==2 and e['build_count']==old['build_count'] and e['keys']==old['keys']
   assert after['pid']==before['pid'] and after['session']==before['session']
   print('Runtime detach/attach retains PID/editor/keys/preedit without rebuild:',w,h,'passed',flush=True)
  finally:a.close()
