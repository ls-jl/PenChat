#!/usr/bin/env python3
"""Portrait pages/real editor controls with the same native runtime as landscape."""
import json,os,pathlib,signal,sqlite3,tempfile,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from test_photo_editor import begin
from test_image_viewer import fixture,open_photo
from settings_ui import activate
from image_gestures import pinch,drag,action
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/portrait';OUT.mkdir(parents=True,exist_ok=True);t.port=1
SCREEN={'width':568,'height':1210,'direction':180,'tp_direction':180}
def shot(a,name):
 p=a.directory/'lvgl-diagnostic.png';p.unlink(missing_ok=True);a.process.send_signal(signal.SIGUSR1);t.wait_for(p.exists);time.sleep(.1)
 (OUT/(name+'.png')).write_bytes(p.read_bytes());(OUT/(name+'.json')).write_text(json.dumps(a.diag(),ensure_ascii=False,indent=2))
def swipe(a,delta):
 start=1000 if delta<0 else 150;end=start+delta
 for i in range(9):a.send('touch',x=400,y=start+(end-start)*i//8,pressed=True);time.sleep(.025)
 time.sleep(.1);a.send('touch',x=400,y=end,pressed=False);time.sleep(.2)
def top(a):
 for _ in range(40):
  if a.diag()['scroll_y']==0:return
  swipe(a,300)
 raise AssertionError('Title unreachable')
def back(a):
 for x in range(12,160,16):a.send("touch",x=x,y=600,pressed=True);time.sleep(.025)
 a.send("touch",x=160,y=600,pressed=False);time.sleep(.15)
def settings(a):
 for _ in range(12):
  d=a.diag()
  if d['view']=='settings':return
  if d['view']=='chat':
   if not d['navigation_open']:a.tap(40,32)
   a.tap(300,1180)
  else:back(a)
 raise AssertionError('Settings unreachable')
def setting(a,key):return activate(a.diag,a.tap,lambda delta:swipe(a,delta),key)
def key(a,key):
 k=next(k for k in a.diag()['editor']['keys'] if k['key']==key);a.tap(k['x'],k['y'])
with tempfile.TemporaryDirectory(prefix='penchat-portrait-') as folder:
 p=pathlib.Path(folder);seed(p,1)
 with sqlite3.connect(p/'penchat.db') as db:
  db.execute("insert into app_settings values('presets_enabled','true',1)")
  db.execute("insert into app_settings values('chat_presets',?,1)",(json.dumps([{'id':'default','name':'默认预设','prompt_mode':'inherit'}]),))
  db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('archive','竖屏归档中的完整标题','archived',1,1,1)")
  db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('archive-message','archive',1,'user','complete','text','archived fixture',1,1)")
 a=t.Native(BINARY,p,screen=SCREEN,official_adapter=False)
 try:
  shot(a,'chat');settings(a);shot(a,'settings')
  rows=a.diag()['settings']['rows'];last=-1
  for row in sorted(rows.values(),key=lambda r:r['bounds'][1]):
   x,y,r,b=row['bounds'];assert x==16 and r==551 and y>last;last=b
  for target in ['models','presets','keyboard','font_size','transfer','feedback','update','archives','http_idle']:
   settings(a);setting(a,target);time.sleep(.1);shot(a,target)
   page=a.diag()['page'];assert page['heading'][0]>=68 and page['viewport'][0]==0
   for bounds in page['actions'].values():assert 0<=bounds[0]<bounds[2]<568
   if target=='models':
    # Provider list and model settings are real controls, not an internal show command.
    a.tap(480,120);assert a.diag()['view']=='model';shot(a,'model')
    a.page_action('参数映射');shot(a,'model-mappings');a.page_action('保存')
    a.page_action('添加 Provider');shot(a,'provider-add')
    a.page_action('api_compatibility');shot(a,'api-compatibility');back(a)
    a.tap(280,255);t.wait_for(lambda:a.diag()['modal']);shot(a,'protocol-picker');a.tap(280,1010)
    a.page_action('anthropic_connection');shot(a,'anthropic-connection');back(a)
   if target=='presets':a.page_action('新增预设');shot(a,'preset-editor')
  settings(a);setting(a,'reasoning');shot(a,'reasoning-picker');a.tap(280,1165);t.wait_for(lambda:not a.diag()['modal'])
  settings(a);back(a);a.tap(540,200)
  assert a.diag()['view']=='chat' and not a.diag()['navigation_open']
  a.attachment_action('models');shot(a,'model-picker');a.tap(200,68)
  a.attachment_action('presets');shot(a,'preset-picker');a.tap(200,66)
  a.attachment_action('effort');a.tap(466,1176);t.wait_for(lambda:a.diag()['popup']=='effort');shot(a,'effort-picker');a.tap(562,100)
  a.attachment_action('session_info');t.wait_for(lambda:a.diag()['view']=='session_info' and not a.diag()['session_info'].get('busy',False));time.sleep(.2);shot(a,'session-info');back(a)
  a.tap(280,1175);t.wait_for(lambda:a.diag()['editor']['active']);shot(a,'keyboard-zh26')
  for c in 'nihao':key(a,c)
  t.wait_for(lambda:a.diag()['editor']['candidate_count']>0 and not a.diag()['editor']['pending'])
  shot(a,'keyboard-candidates');a.editor_action('expand');shot(a,'keyboard-expanded')
  a.editor_action('expand');key(a,'space');t.wait_for(lambda:a.diag()['editor']['length']>0)
  a.tap(34,30);a.tap(34,30);assert a.diag()['editor']['selected']
  a.long_press(34,30);t.wait_for(lambda:a.diag()['editor']['menu']);shot(a,'keyboard-selection-menu')
  a.tap(140,30);a.tap(150,100)
  a.send('touch',x=34,y=30,pressed=True);time.sleep(.5);a.send('touch',x=120,y=30,pressed=True)
  t.wait_for(lambda:a.diag()['editor']['magnifier']);shot(a,'keyboard-magnifier')
  a.send('touch',x=280,y=950,pressed=True);time.sleep(.1)
  assert a.diag()['editor']['cursor_drag'] and a.diag()['editor']['text_scroll']==0
  a.send('touch',x=280,y=950,pressed=False);time.sleep(.1)
  key(a,'language');shot(a,'keyboard-en26');key(a,'mode');shot(a,'keyboard-number');key(a,'symbols');shot(a,'keyboard-symbol');key(a,'mode')
  k=next(k for k in a.diag()['editor']['keys'] if k['key']=='language');a.long_press(k['x'],k['y']);a.editor_action('layout:2');shot(a,'keyboard-zh9')
  key(a,'t9:6');key(a,'t9:4');t.wait_for(lambda:a.diag()['editor']['candidate_count']>0);shot(a,'keyboard-zh9-candidates')
  a.editor_action('clipboard');shot(a,'keyboard-clipboard');a.tap(515,40)
  a.editor_action('official');request=a.receive('keyboard.open');a.send('keyboard.result',requestId=request['requestId'],confirmed=False);t.wait_for(lambda:not a.diag()['keyboard'])
  assert a.diag()['editor']['active'];a.editor_action('cancel');t.wait_for(lambda:not a.diag()['editor']['active'])
  begin(a,p,w=800,h=600);shot(a,'crop');x,y,w,h=a.diag()['crop_selection'];pinch(a,x+w/2,y+h/2,scale=2,crop=True);shot(a,'crop-zoom');action(a,'reset');assert a.diag()['crop_source_rect']==[0,0,800,600]
  action(a,'confirm');t.wait_for(lambda:a.diag()['view']=='chat');shot(a,'pending-photo')
 finally:a.close()
with tempfile.TemporaryDirectory(prefix='penchat-portrait-viewer-') as folder:
 p=pathlib.Path(folder);fixture(p);a=t.Native(BINARY,p,screen=SCREEN,official_adapter=False)
 try:
  open_photo(a);t.wait_for(lambda:a.diag()['image_viewer']['loaded']);shot(a,'viewer');pinch(a,284,605,scale=3);shot(a,'viewer-zoom')
  a.tap(530,30);t.wait_for(lambda:not a.diag()['image_viewer']['open'])
  before=a.diag()['session']
  for y in range(1000,749,-10):a.send('touch',x=280,y=y,pressed=True);time.sleep(.025)
  assert a.diag()['pull_progress']>=.99;shot(a,'pull-progress')
  a.send('touch',x=280,y=750,pressed=False);t.wait_for(lambda:a.diag()['session']!=before)
  fresh=a.diag()['session']
  for y in range(1000,749,-10):a.send('touch',x=280,y=y,pressed=True);time.sleep(.025)
  assert not a.diag()['pull_active'];a.send('touch',x=280,y=750,pressed=False)
  assert a.diag()['session']==fresh and a.diag()['summary']['sessions']==1

 finally:a.close()
print('Portrait: settings/forms, native keyboard modes/candidates/clipboard/official cancel, full-pixel crop and multitouch viewer passed.')
