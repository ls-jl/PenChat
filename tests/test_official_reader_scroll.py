#!/usr/bin/env python3
"""Official IME permits only single-contact scrolling in the retained reader."""
import pathlib,tempfile,time
import test_lvgl_app as ui
from test_chat_style import seed
ui.port=1
with tempfile.TemporaryDirectory(prefix='official-reader-') as folder:
 seed(folder,'\n\n'.join('阅读区域第%d段。'%i for i in range(100)))
 a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',pathlib.Path(folder),official_adapter=False,screen={'width':568,'height':1210})
 try:
  ui.wait_for(lambda:not a.diag()['chat_render_pending'])
  b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
  ui.wait_for(lambda:a.diag()['editor']['active']);a.editor_action('official');ident=a.receive('keyboard.open')['requestId']
  def lease():a.send('keyboard.layout',requestId=ident,height=590,foreground=True)
  def touch(y,down=True,x=200,points=None):
   lease();a.send('touch',x=x,y=y,pressed=down,**({'points':points} if points is not None else {}));time.sleep(.025)
  lease();time.sleep(.1);before=a.diag()['scroll_y'];session=a.diag()['editor']['session']
  for y in [180,210,250,300,340]:touch(y)
  touch(340,False);assert a.diag()['scroll_y']<before
  fixed=a.diag()['scroll_y']
  for y in [900,850,700,350,250]:touch(y)
  touch(250,False);assert a.diag()['scroll_y']==fixed,'Keyboard-origin touch reached reader'
  touch(200,points=[{'id':1,'x':200,'y':200},{'id':2,'x':220,'y':210}])
  touch(300);touch(300,False);assert a.diag()['scroll_y']==fixed
  a.send('system.overlay.status',panel_visible=True)
  for y in [180,250,340]:touch(y)
  touch(340,False);assert a.diag()['scroll_y']==fixed
  a.send('system.overlay.status',panel_visible=False);lease()
  time.sleep(.82)
  for y in [180,250,340]:a.send('touch',x=200,y=y,pressed=True)
  a.send('touch',x=200,y=340,pressed=False);time.sleep(.05)
  assert a.diag()['scroll_y']==fixed,'Expired official lease accepted touch'
  lease();a.send('visibility',visible=False)
  for y in [180,250,340]:touch(y)
  touch(340,False);assert a.diag()['scroll_y']==fixed
  a.send('visibility',visible=True);lease()
  assert a.diag()['editor']['session']==session and a.diag()['keyboard']
  assert not a.diag()['editor']['ocr_active'] and not a.diag()['generating']
  print('OFFICIAL_READER_SCROLL_ONLY_KEYBOARD_MULTITOUCH_OVERLAY_ISOLATION_OK')
 finally:a.close()
