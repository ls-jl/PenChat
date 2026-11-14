#!/usr/bin/env python3
"""Shared native theme/context menus; isolated data, no device or paid requests."""
import json,os,pathlib,sqlite3,tempfile,time,signal
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
from provider_ui import activate
ROOT=t.ROOT;t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
OUT=ROOT/'.work/ui-unification/theme/legacy';OUT.mkdir(parents=True,exist_ok=True)
class App(t.Native):
 def __init__(self,p,w,h,binary=BINARY,onboarding=False):
  self.w=w;self.h=h
  super().__init__(binary,p,seed_model=False,official_adapter=False,onboarding=onboarding,screen={'width':w,'height':h,'direction':0})
 def tapb(self,b):self.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
 def settings_swipe(self,delta):
  start=min(self.h-35,220) if delta<0 else 75;end=max(20,min(self.h-20,start+delta))
  for i in range(7):self.send('touch',x=self.w//2,y=start+(end-start)*i//6,pressed=True);time.sleep(.02)
  time.sleep(.1);self.send('touch',x=self.w//2,y=end,pressed=False);time.sleep(.15)
 def open_navigation(self):
  self.to_chat();time.sleep(.15);self.nav_tap()
  t.wait_for(lambda:self.diag()['navigation_open'])
 def nav_tap(self):
  d=self.diag()
  if self.h<=self.w:self.tap(40,32);return
  heading=(d.get('page') or {}).get('heading')
  y=heading[1]+16 if heading else d['safe_insets'][1]+24
  self.tap(d.get('screen_safety',{}).get('navigation_x',d['safe_insets'][0])+22,y)
 def open_settings(self):
  self.open_navigation();self.tap(self.w*2//3-70,self.h-(48 if self.h>self.w else 30));t.wait_for(lambda:self.diag()['view']=='settings')
 def top(self):
  for _ in range(40):
   if self.diag()['scroll_y']<=0:return
   self.settings_swipe(130)
 def to_chat(self):
  for _ in range(12):
   d=self.diag()
   if d['navigation_open']:self.tap(self.w-12,85)
   elif d['view']=='chat':time.sleep(.15);return
   else:
    self.top();self.nav_tap()
  raise AssertionError(self.diag()['view'])
 def key(self,name):
  e=self.diag()['editor'];k=next(k for k in e['keys'] if k['key']==name);s=e['coordinate_space'];x,y=k['x'],k['y']
  if s['scaled']:x=round(x*s['native_width']/s['width']);y=round(y*s['native_height']/s['height'])
  self.tap(x,y)
 def shot(self,name):capture(self,OUT/f'{self.w}x{self.h}-{name}.png')
def fixture(p):
 seed(p,1)
 with sqlite3.connect(p/'penchat.db') as db:
  for k,v in {'auto_title_enabled':False,'presets_enabled':True,'new_chat_web':'off'}.items():db.execute('insert or replace into app_settings values(?,?,1)',(k,json.dumps(v)))
  db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('s','可选择的完整历史标题测试','active',1,1,1)")
  db.execute("insert or replace into app_settings values('active_session_id','\"s\"',1)")
  for i,(id,role,text) in enumerate([('u','user','hello 世界'),('a','assistant','收到。')],1):
   db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'s',?,?,'complete','text',?,1,1)",(id,i,role,text))
  db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('other','另一条历史会话','active',1,0,0)")
  db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('other-a','other',1,'assistant','complete','text','参考',1,1)")

def run(w,h):
 with tempfile.TemporaryDirectory(prefix='penchat-theme-') as folder:
  p=pathlib.Path(folder);fixture(p)
  (p/'feedback.json').write_text('{}');(p/'update.json').write_text(json.dumps({'root':'http://127.0.0.1:1/penchat-rk/','nodes':['127.0.0.1'],'auto':False}))
  os.environ['PENCHAT_TEST_FEEDBACK_CONFIG']=str(p/'feedback.json');os.environ['PENCHAT_TEST_UPDATE_CONFIG']=str(p/'update.json')
  os.environ['PENCHAT_TEST_Y07_ROUNDED']='1' if h>w else '0'
  a=App(p,w,h)
  try:
   a.top();b=a.diag()['message_interactions']['bubbles']['u'];a.long_press((b[0]+b[2])//2,(b[1]+b[3])//2)
   t.wait_for(lambda:a.diag()['context_menu']['kind']=='message');d=a.diag();b=d['context_menu']['bounds'];assert b[2]-b[0]+1<=320 and b[3]-b[1]+1<h*.8
   a.shot('message-menu');actions=d['message_interactions']['actions'];assert actions['copy'][1]<actions['select'][1]<actions['edit'][1]
   assert all(b[2]-b[0]+1>=44 and b[3]-b[1]+1>=44 for b in actions.values())
   # Opening release cannot select; cancelled outside touch cannot click the page.
   assert d['message_interactions']['context_id']=='u'
   a.tap(w-4,h-4);assert not a.diag()['modal'] and not a.diag()['editor']['active']
   a.open_navigation();row=next(r for r in a.diag()['history_rows'] if r['id']=='s');a.long_press(row['x'],row['y'])
   t.wait_for(lambda:a.diag()['context_menu']['kind']=='history');d=a.diag();a.shot('history-menu');actions=d['history_actions'];assert actions['rename'][1]<actions['pin'][1]<actions['archive'][1]<actions['delete'][1]<actions['export'][1]
   assert all(b[2]-b[0]+1>=44 and b[3]-b[1]+1>=44 for b in actions.values())
   # All actions remain reachable in the menu's own scroll list.
   box=d['context_menu']['list']
   for _ in range(15):
    b=a.diag()['history_actions']['export'];box=a.diag()['context_menu']['list']
    if b[1]>=box[1] and b[3]<=box[3]:break
    x=(box[0]+box[2])//2;start=box[3]-10;end=max(box[1]+10,start-70)
    for i in range(6):a.send('touch',x=x,y=start+(end-start)*i//5,pressed=True);time.sleep(.02)
    a.send('touch',x=x,y=end,pressed=False);time.sleep(.15)
   else:raise AssertionError('Last history action unreachable')
   a.shot('history-menu-bottom');a.tap(w-4,h-4);assert not a.diag()['modal'];a.to_chat()
   a.open_navigation();row=next(r for r in a.diag()['history_rows'] if r['id']=='other');a.long_press(row['x'],row['y'])
   d=a.diag();assert d['session']=='s' and d['history_context']=='other'
   assert {r['id'] for r in d['history_rows'] if r['highlighted']}=={'other'}
   assert set(d['history_actions'])=={'rename','pin','archive','delete','export'}
   a.shot('history-target-highlight');a.tap(w-4,h-4)
   assert {r['id'] for r in a.diag()['history_rows'] if r['highlighted']}=={'s'}
   a.to_chat()
   a.open_navigation();row=next(r for r in a.diag()['history_rows'] if r['id']=='s');a.long_press(row['x'],row['y']);t.wait_for(lambda:a.diag()['context_menu']['kind']=='history')
   a.history_action('rename');t.wait_for(lambda:a.diag()['editor']['active']);a.shot('history-rename')
   assert a.diag()['editor']['length']>0
   a.editor_action('cancel' if 'cancel' in a.diag()['editor']['actions'] else 'done');a.to_chat()
   a.open_settings();a.shot('settings');a.setting('font_size');a.shot('font-size');a.to_chat();a.open_settings();a.setting('models');a.shot('providers')
   activate(a,'provider-open:p');a.shot('provider-form');a.to_chat()
   if h>w:
    a.tapb(a.diag()['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active']);a.key('n');a.key('i');t.wait_for(lambda:not a.diag()['editor']['pending'])
    before=a.diag()['editor'];session=before['session'];preedit=before['preedit_length'];keys=before['keys'];ocr=before['ocr_request'];builds=before['build_count']
    d=a.diag();a.tap(d['composer_bounds'][0]+46,d['send_center_y2']//2);t.wait_for(lambda:a.diag()['popup']=='attachments');time.sleep(.22)
    d=a.diag();assert d['editor']['active'] and d['editor']['session']==session and d['editor']['preedit_length']==preedit
    assert d['editor']['keys']==keys and d['editor']['ocr_request']==ocr and d['editor']['build_count']==builds
    a.shot('keyboard-plus')
    a.tap(w-4,h-4);assert not a.diag()['attachments'] and a.diag()['editor']['preedit_length']==preedit
    a.key('h');assert a.diag()['editor']['preedit_length']==preedit+1
    # Picker choices must keep composition and the exact keyboard session.
    d=a.diag();a.tap(d['composer_bounds'][0]+46,d['send_center_y2']//2);time.sleep(.23);a.tapb(a.diag()['attachment_actions']['models']);t.wait_for(lambda:a.diag()['popup']=='models')
    d=a.diag();assert d['editor']['session']==session and d['editor']['preedit_length']==preedit+1;a.shot('keyboard-model-picker')
    # First model after the provider caption, in the retained reader panel.
    b=d['popup_bounds'];a.tap(b[0]+100,b[1]+102);time.sleep(.2)
    d=a.diag();assert d['editor']['active'] and d['editor']['session']==session and d['editor']['preedit_length']==preedit+1
    assert d['active_model']=='b'
    a.shot('keyboard-after-model')
  finally:a.close()
 print('Theme, bounded menus and retained editor:',w,h,'passed',flush=True)
if __name__=='__main__':
 for size in [(640,260),(960,266),(568,1210)]:run(*size)
