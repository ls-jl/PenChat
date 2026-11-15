#!/usr/bin/env python3
"""Official IME permits only single-contact scrolling in the retained reader."""
import pathlib,tempfile,time,sqlite3
import test_lvgl_app as ui
from test_chat_style import seed
ui.port=1
with tempfile.TemporaryDirectory(prefix='official-reader-') as folder:
 seed(folder,'\n\n'.join('阅读区域第%d段。'%i for i in range(100)))
 with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:db.execute("insert into app_settings(key,value_json,updated_at) values('theme_mode','\"light\"',1)")
 a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',pathlib.Path(folder),official_adapter=False,screen={'width':568,'height':1210})
 try:
  ui.wait_for(lambda:not a.diag()['chat_render_pending'])
  b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
  ui.wait_for(lambda:a.diag()['editor']['active']);a.editor_action('official');request=a.receive('keyboard.open');ident=request['requestId']
  def lease():a.send('keyboard.layout',requestId=ident,height=590,foreground=True)
  def touch(y,down=True,x=200,points=None):
   lease();a.send('touch',x=x,y=y,pressed=down,**({'points':points} if points is not None else {}));time.sleep(.025)
  lease();time.sleep(.1);initial=a.diag();before=initial['scroll_y'];session=initial['editor']['session']
  assert initial['chat_header']['reader_bounds'][3]==initial['height']-1
  parts=[b for r in initial['text_selection']['regions'] if r['key']=='reply0' for b in r['parts'] if b[3]>=b[1]]
  assert parts and max(b[3] for b in parts)<=initial['composer_bounds'][1]-16,'Last line is obscured by composer'
  for y in [180,210,250,300,340]:touch(y)
  touch(340,False);assert a.diag()['scroll_y']<before
  scrolled=a.diag()
  assert scrolled['chat_header']['reader_bounds'][3]>scrolled['composer_bounds'][3]
  released=a.diag()['scroll_y'];time.sleep(.12);lease()
  assert a.diag()['scroll_y']<released,'Release stopped without inertia'
  # A new contact, including on the keyboard, stops the glide immediately.
  touch(900);stopped=a.diag()['scroll_y'];time.sleep(.12);lease()
  assert a.diag()['scroll_y']==stopped
  touch(900,False)
  # Side margins are valid origins, and a captured drag may cross the old body edge.
  before_wide=a.diag()['scroll_y']
  for y in [150,220,350,500]:touch(y,x=2)
  touch(500,False,x=2);assert a.diag()['scroll_y']<before_wide
  touch(900);touch(900,False)
  fixed=a.diag()['scroll_y']
  from image_gestures import capture
  out=ui.ROOT/'.work/composer-transparent';out.mkdir(parents=True,exist_ok=True)
  capture(a,out/'reader-behind-capsule.png')
  for y in [900,850,700,350,250]:touch(y)
  touch(250,False);assert a.diag()['scroll_y']==fixed,'Keyboard-origin touch reached reader'
  touch(200,points=[{'id':1,'x':200,'y':200},{'id':2,'x':220,'y':210}])
  touch(300);touch(300,False);assert a.diag()['scroll_y']==fixed
  for y in [180,220,270,320]:touch(y)
  touch(320,False)
  a.send('system.overlay.status',panel_visible=True);time.sleep(.04)
  fixed=a.diag()['scroll_y'];time.sleep(.12);assert a.diag()['scroll_y']==fixed
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
