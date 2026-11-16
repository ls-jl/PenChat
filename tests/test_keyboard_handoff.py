#!/usr/bin/env python3
"""Real LVGL: direct official entry, normal hide and failed handoff recovery."""
import pathlib,tempfile,time
import test_lvgl_app as ui
from test_pinyin_ui import open_editor
ui.port=1
for w,h in [(960,266),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-keyboard-handoff-') as tmp:
  a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',tmp,official_adapter=False,screen={'width':w,'height':h})
  try:
   a.open_settings();a.setting('keyboard');a.setting('quick_official_keyboard')
   ui.wait_for(lambda:a.diag()['editor']['official_shortcut']['saved'])
   a.to_chat();open_editor(a)
   assert 'official' in a.diag()['editor']['actions']
   # Tap the actual action, without opening the globe menu.
   b=a.diag()['editor']['actions']['official'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   request=a.receive('keyboard.open')['requestId']
   a.send('keyboard.result',requestId=request,confirmed=False,text='retained voice input')
   ui.wait_for(lambda:not a.diag()['editor']['active'])
   s=a.diag();assert s['draft_length']==20 and not s['composer_hidden'],s
   open_editor(a);a.editor_action('official');request=a.receive('keyboard.open')['requestId']
   a.send('keyboard.result',requestId=request,confirmed=False,error='fixture unavailable')
   ui.wait_for(lambda:a.diag()['editor']['active'] and not a.diag()['editor']['official'])
   assert 'official' in a.diag()['editor']['actions']
   a.editor_action('done');ui.wait_for(lambda:not a.diag()['editor']['active'])
   assert not a.diag()['composer_hidden']
   # The underlay stays allocated/visible; only native input is yielded.
   a.send('window.focus',focused=False,sequence=1)
   a.send('system.overlay.status',panel_visible=True)
   assert a.diag()['display_visible'] and not a.diag()['input_focused']
   assert not list(a.directory.glob('overlay_*.png'))
   a.send('system.overlay.status',panel_visible=False)
   a.send('window.focus',focused=True,sequence=2)
   ui.wait_for(lambda:a.diag()['input_focused'])
   print('Keyboard handoff, retained draft/composer and underlay:',w,h,'passed',flush=True)
  finally:a.close()

from test_chat_style import seed
for w,h in [(960,266),(640,260)]:
 with tempfile.TemporaryDirectory(prefix='penchat-keyboard-reading-') as tmp:
  seed(tmp,'\n\n'.join('阅读位置保留测试 '+str(i) for i in range(150)))
  a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',tmp,official_adapter=False,screen={'width':w,'height':h})
  try:
   for _ in range(2):a.settings_swipe(85);time.sleep(.3)
   s=a.diag();assert s['composer_hidden'] and not s['at_latest_bottom']
   b=s['composer_shortcut']['bounds'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
   a.tap(x,y);a.tap(x,y);time.sleep(.2)
   s=a.diag();b=s['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   ui.wait_for(lambda:a.diag()['editor']['active'])
   a.editor_action('official');request=a.receive('keyboard.open')['requestId']
   a.send('keyboard.result',requestId=request,confirmed=False,text='voice draft')
   ui.wait_for(lambda:not a.diag()['editor']['active']);time.sleep(.5)
   s=a.diag();assert not s['composer_hidden'] and s['draft_length']==11,s
   print('Scrolled conversation keeps completed composer:',w,h,'passed',flush=True)
  finally:a.close()
