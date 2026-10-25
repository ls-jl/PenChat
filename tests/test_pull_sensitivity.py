#!/usr/bin/env python3
"""Native preference slider and gesture distance, preserving reader scaling/guards."""
import json,os,pathlib,signal,sqlite3,tempfile,time
from test_lvgl_app import Native,ROOT,wait_for
from test_chat_style import seed
from settings_ui import locate
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
OUT=ROOT/'.work/pull-sensitivity';OUT.mkdir(parents=True,exist_ok=True)
def run(w,h):
 with tempfile.TemporaryDirectory(prefix='penchat-pull-offset-') as folder:
  p=pathlib.Path(folder);seed(p,'上滑灵敏度测试内容')
  a=Native(BINARY,p,screen={'width':w,'height':h,'direction':0},official_adapter=False)
  def shot(name):
   file=p/'lvgl-diagnostic.png'
   if file.exists():file.unlink()
   a.process.send_signal(signal.SIGUSR1);wait_for(file.exists)
   (OUT/f'{w}-{h}-{name}.png').write_bytes(file.read_bytes())
  def adjust(value):
   d=a.diag()['pull_preferences'];b=d['slider'];assert b
   x=round(b[0]+(value-d['minimum'])*(b[2]-b[0])/(d['maximum']-d['minimum']))
   a.tap(x,(b[1]+b[3])//2);wait_for(lambda:a.diag()['pull_preferences']['draft']==value)
  def distance():
   a.send('touch',x=500,y=h-34,pressed=True);d=a.diag()
   a.send('touch',x=500,y=h-34,pressed=False,cancelled=True);time.sleep(.2)
   return d['pull_full_distance'],d
  try:
   base,state=distance();assert state['pull_sensitivity_offset']==0 and state['pull_frozen_offset']==0
   assert state['pull_pause_ms']==500 and state['pull_reader_height']==h-58
   a.open_settings();locate(a.diag,a.settings_swipe,'pull_sensitivity');shot('settings-row')
   a.setting('pull_sensitivity');before=a.diag()['scroll_y'];shot('baseline')
   adjust(10);shot('positive')
   # Cancel does not save; returning to the same row does not change its scroll.
   a.tap(w-70,h-44);assert not a.diag()['modal'] and a.diag()['pull_sensitivity_offset']==0
   assert a.diag()['scroll_y']==before
   a.setting('pull_sensitivity');adjust(10);a.tap(w-190,h-44)
   wait_for(lambda:not a.diag()['modal']);assert a.diag()['pull_sensitivity_offset']==10
   assert a.diag()['scroll_y']==before
   with sqlite3.connect(p/'penchat.db') as db:
    assert json.loads(db.execute("select value_json from app_settings where key='pull_sensitivity_offset'").fetchone()[0])==10
   a.to_chat();positive,state=distance();assert positive<base and state['pull_frozen_offset']==10
   assert abs(positive*1.1-base)<=1
   a.open_settings();a.setting('pull_sensitivity');adjust(-10);shot('negative')
   a.tap(w-190,h-44);wait_for(lambda:not a.diag()['modal']);a.to_chat()
   negative,state=distance();assert negative>base and state['pull_frozen_offset']==-10
   assert abs(negative*.9-base)<=1
  finally:a.close()
  a=Native(BINARY,p,screen={'width':w,'height':h,'direction':0},official_adapter=False)
  try:
   assert a.diag()['pull_sensitivity_offset']==-10
   a.open_settings();a.setting('pull_sensitivity');assert a.diag()['pull_preferences']['draft']==-10
   a.tap(167,h-44);assert a.diag()['pull_preferences']['draft']==0;shot('reset')
   a.tap(w-190,h-44);wait_for(lambda:not a.diag()['modal']);a.to_chat()
   restored,state=distance();assert restored==base and state['pull_sensitivity_offset']==0
   original=a.diag()['session'];start=h-34
   a.send('touch',x=500,y=start,pressed=True);frozen=a.diag()['pull_full_distance']
   for moved in (30,60,90,frozen-1):
    a.send('touch',x=500,y=start-moved,pressed=True);time.sleep(.03)
   assert 0<a.diag()['pull_progress']<100
   a.send('touch',x=500,y=start-frozen,pressed=True);assert a.diag()['pull_progress']==100
   a.send('touch',x=500,y=start-frozen,pressed=False);wait_for(lambda:a.diag()['session']!=original)
   blank=a.diag()['session'];time.sleep(.2)
   for moved in (0,40,80,frozen):a.send('touch',x=500,y=start-moved,pressed=True);time.sleep(.03)
   assert not a.diag()['pull_active'];a.send('touch',x=500,y=start-frozen,pressed=False)
   assert a.diag()['session']==blank
  finally:a.close()
for size in ((960,266),(936,280)):run(*size)
with tempfile.TemporaryDirectory(prefix='penchat-pull-saved-') as folder:
 p=pathlib.Path(folder);seed(p,'读取历史设置')
 for raw,expected in [('null',0),('\"fast\"',0),('false',0),('-900',-25),('900',25)]:
  with sqlite3.connect(p/'penchat.db') as db:
   db.execute("insert or replace into app_settings values('pull_sensitivity_offset',?,1)",(raw,))
  a=Native(BINARY,p,official_adapter=False)
  try:assert a.diag()['pull_sensitivity_offset']==expected,(raw,a.diag()['pull_sensitivity_offset'])
  finally:a.close()
print('Pull sensitivity: signed offsets, cancel/save/reset, restart, frozen scaled geometry, full-release and empty guard passed at both sizes.')
