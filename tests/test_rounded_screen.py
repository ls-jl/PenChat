#!/usr/bin/env python3
"""Y07 logical corner envelope, native controls and synthetic host screenshots."""
import json,math,os,pathlib,sqlite3,tempfile,time
from test_ui_theme import App,fixture,t
from test_photo_editor import begin
from image_gestures import capture
from provider_ui import activate
OUT=t.ROOT/'.work/y07-rounded-20261113/screens';OUT.mkdir(parents=True,exist_ok=True)
W,H=568,1210

def safe(bounds):
 x1,y1,x2,y2=bounds;r=math.ceil(W*.20);m=math.ceil(W*.015)
 assert x2-x1+1>=44 and y2-y1+1>=44,bounds
 assert y1>=m and y2<H-m,bounds
 edge=min(y1,H-1-y2)
 inset=m if edge>=r else math.ceil(r-math.sqrt(r*r-(r-edge)**2)+m)
 assert x1>=inset and x2<W-inset,(bounds,inset)

def shot(a,name):capture(a,OUT/(name+'.png'))
def prepare(p):
 fixture(p);(p/'feedback.json').write_text('{}');(p/'update.json').write_text(json.dumps({'root':'http://127.0.0.1:1/penchat-rk/','nodes':['127.0.0.1'],'auto':False}))
 os.environ.update(PENCHAT_TEST_Y07_ROUNDED='1',PENCHAT_TEST_FEEDBACK_CONFIG=str(p/'feedback.json'),PENCHAT_TEST_UPDATE_CONFIG=str(p/'update.json'))

with tempfile.TemporaryDirectory(prefix='penchat-rounded-') as directory:
 p=pathlib.Path(directory);prepare(p);a=App(p,W,H)
 try:
  d=a.diag();assert d['screen_safety']['rounded'] and d['screen_safety']['radius']==114
  x,y=d['send_center_x2']/2,d['send_center_y2']/2
  safe([int(x-27),int(y-27),int(x+26),int(y+26)]);shot(a,'chat')
  a.tapb(d['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active']);a.key('n');a.key('i');time.sleep(.15);shot(a,'keyboard')
  e=a.diag()['editor'];g=next(k for k in e['keys'] if k['key']=='language');a.long_press(g['x'],g['y'],.58);t.wait_for(lambda:a.diag()['editor']['layout_menu']);shot(a,'globe-menu');a.editor_action('layout:0')
  for _ in range(3):
   if not a.diag()['editor']['active']:break
   a.editor_action('done');time.sleep(.15)
  a.open_navigation();shot(a,'history');a.to_chat();a.open_settings();shot(a,'settings')
  a.setting('models');activate(a,'provider-open:p');shot(a,'provider');a.to_chat()
  d=a.diag();a.tap(d['composer_bounds'][0]+46,d['send_center_y2']//2);time.sleep(.25);a.tapb(a.diag()['attachment_actions']['camera']);t.wait_for(lambda:a.diag()['view']=='camera');time.sleep(.3)
  if a.diag()['modal']:a.tap(W//2,H-a.diag()['safe_insets'][3]-22)
  d=a.diag()
  for key,b in d['camera_controls'].items():
   if key!='preview':safe(b)
  shot(a,'camera');begin(a,p);d=a.diag()
  for b in d['crop_actions'].values():safe(b)
  shot(a,'crop')
 finally:a.close()
 with sqlite3.connect(p/'penchat.db') as db:db.execute("insert or replace into app_settings values('onboarding',?,1)",(json.dumps({'version':1,'complete':False,'step':0}),))
 a=App(p,W,H,onboarding=True)
 try:
  for step in range(7):
   t.wait_for(lambda:a.diag()['view']=='onboarding' and a.diag()['onboarding']['step']==step)
   d=a.diag()['onboarding'];time.sleep(.15)
   for b in d['actions'].values():safe(b)
   shot(a,'guide-'+str(step+1))
   if step<6:a.tapb(d['actions']['next'])
 finally:a.close()
print('Y07 explicit mask, real control bounds, retained keyboard, crop and seven guide pages passed')
