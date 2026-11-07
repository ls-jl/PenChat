#!/usr/bin/env python3
"""Real first-run LVGL pages, isolated DBs, no synthetic business side effects."""
import json,os,pathlib,signal,sqlite3,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
ROOT=t.ROOT;OUT=ROOT/'.work/onboarding';OUT.mkdir(parents=True,exist_ok=True);t.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
from test_onboarding_helpers import action
def shot(a,name):
 p=a.directory/'lvgl-diagnostic.png'
 if p.exists():p.unlink()
 a.process.send_signal(signal.SIGUSR1);t.wait_for(p.exists);(OUT/(name+'.png')).write_bytes(p.read_bytes())
def counts(p):
 with sqlite3.connect(p/'penchat.db') as db:
  return {table:db.execute('select count(*) from '+table).fetchone()[0] for table in ('chat_sessions','chat_messages','providers','model_configs','message_assets')}
def marker(p):
 with sqlite3.connect(p/'penchat.db') as db:return json.loads(db.execute("select value_json from app_settings where key='onboarding'").fetchone()[0])
for w,h in [(960,266),(936,280),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-onboard-') as folder:
  p=pathlib.Path(folder);a=t.Native(binary,p,seed_model=False,onboarding=True,screen={'width':w,'height':h,'direction':0,'tp_direction':0})
  try:
   assert a.diag()['view']=='onboarding';baseline=counts(p)
   for step in range(7):
    d=a.diag()['onboarding'];assert d['step']==step
    assert d['renderer']=='software_3d' and not d['render_failed'],d
    assert 0<d['render_bytes']<=640*360*12 and 10<=d['render_triangles']<=80
    assert ('finish' in d['actions'])==(step>=3)
    assert ('back' in d['actions'])==(step>0)
    for b in d['actions'].values():assert b[0]>=0 and b[1]>=0 and b[2]<w and b[3]<h and b[3]-b[1]+1>=44
    action(a,'replay');shot(a,f'{w}-{h}-{step}-start')
    # Verify animation really progresses, can pause, and stays paused in background.
    t.wait_for(lambda:a.diag()['onboarding']['phase']>=1,timeout=4);action(a,'pause');before=a.diag()['onboarding'];time.sleep(.12)
    assert a.diag()['onboarding']['elapsed']==before['elapsed']
    assert a.diag()['onboarding']['render_frames']==before['render_frames'];shot(a,f'{w}-{h}-{step}-middle')
    a.send('visibility',visible=False);time.sleep(.15);a.send('visibility',visible=True)
    assert a.diag()['onboarding']['elapsed']==before['elapsed']
    assert a.diag()['onboarding']['render_frames']==before['render_frames'];action(a,'pause')
    t.wait_for(lambda:a.diag()['onboarding']['phase']==3,timeout=6);action(a,'pause');shot(a,f'{w}-{h}-{step}-end')
    assert a.diag()['onboarding']['objects']==before['objects']
    assert not a.diag()['editor']['active'] and not a.diag()['keyboard']
    assert counts(p)==baseline
    if step==0:
     # A real native edge-back gesture cannot escape the mandatory first page.
     for i in range(7):a.send('touch',x=10+i*w//40,y=h//2,pressed=True);time.sleep(.025)
     a.send('touch',x=w//5,y=h//2,pressed=False);time.sleep(.12);assert a.diag()['view']=='onboarding'
    if step==2:
     action(a,'configure');t.wait_for(lambda:a.diag()['view']=='provider');a.tap(40,32)
     t.wait_for(lambda:a.diag()['view']=='onboarding');assert a.diag()['onboarding']['step']==2
    action(a,'next')
    if step<6:t.wait_for(lambda:a.diag()['onboarding']['step']==step+1)
   t.wait_for(lambda:a.diag()['view']=='chat');assert marker(p)['complete'];assert counts(p)==baseline
  finally:a.close()
  a=t.Native(binary,p,seed_model=False,onboarding=True,screen={'width':w,'height':h,'direction':0,'tp_direction':0})
  try:
   assert a.diag()['view']=='chat'
   a.open_settings();a.setting('about');a.tap(w//2 if h>w else 156,h-96 if h>w else h-44)
   t.wait_for(lambda:a.diag()['view']=='onboarding');assert 'finish' in a.diag()['onboarding']['actions']
   action(a,'finish');t.wait_for(lambda:a.diag()['view']=='settings');assert marker(p)['complete']
  finally:a.close()
# Existing conversation, API provider and restart midway through mandatory pages.
with tempfile.TemporaryDirectory(prefix='penchat-onboard-upgrade-') as folder:
 p=pathlib.Path(folder);seed(p,'保留用户原有历史。');a=t.Native(binary,p,onboarding=True)
 try:
  before=a.read_messages();session=a.diag()['session'];baseline=counts(p)
  action(a,'next');assert marker(p)['step']==1
 finally:a.close()
 a=t.Native(binary,p,onboarding=True)
 try:
  assert a.diag()['onboarding']['step']==1;action(a,'next');action(a,'next')
  action(a,'finish');t.wait_for(lambda:a.diag()['view']=='chat')
  assert a.diag()['session']==session and a.read_messages()==before and counts(p)==baseline
 finally:a.close()
# Save failure must not mark a mandatory page or completion as successful.
with tempfile.TemporaryDirectory(prefix='penchat-onboard-save-failure-') as folder:
 p=pathlib.Path(folder);a=t.Native(binary,p,seed_model=False,onboarding=True)
 try:
  with sqlite3.connect(p/'penchat.db') as db:
   db.execute("CREATE TRIGGER reject_guide BEFORE INSERT ON app_settings WHEN NEW.key='onboarding' BEGIN SELECT RAISE(ABORT,'fixture guide failure'); END")
  action(a,'next');t.wait_for(lambda:a.diag()['modal']);assert a.diag()['onboarding']['step']==0
  with sqlite3.connect(p/'penchat.db') as db:db.execute('DROP TRIGGER reject_guide')
  a.tap(890,222);t.wait_for(lambda:not a.diag()['modal']);action(a,'next');assert marker(p)['step']==1
 finally:a.close()
print('Onboarding: seven animated pages / three sizes, mandatory steps, pause/replay/background, configuration return, restart, upgrade, no business writes and About replay passed.')
