#!/usr/bin/env python3
"""Provider compatibility controls: layout, unsaved isolation, persistence, back position."""
import json,os,pathlib,signal,sqlite3,tempfile,time
from test_lvgl_app import Native,ROOT,wait_for
from test_reasoning_ui import seed
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
OUT=ROOT/'.work/api-ui';OUT.mkdir(parents=True,exist_ok=True)
def run(w,h):
 with tempfile.TemporaryDirectory(prefix='penchat-api-ui-') as folder:
  p=pathlib.Path(folder);seed(p,1)
  a=Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
  def shot(name):
   target=p/'lvgl-diagnostic.png'
   if target.exists():target.unlink()
   a.process.send_signal(signal.SIGUSR1);wait_for(target.exists)
   (OUT/f'{w}-{h}-{name}.png').write_bytes(target.read_bytes())
  def locate(name):
   for _ in range(30):
    s=a.diag();b=s['settings']['rows'][name]['bounds']
    if b[1]>=0 and b[3]<h:
     time.sleep(.15);return a.diag()['settings']['rows'][name]['bounds']
    a.settings_swipe(max(-100,min(100,h//2-(b[1]+b[3])//2)))
    time.sleep(.2)
   raise AssertionError(name)
  def taprow(name):
   b=locate(name);a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
  try:
   a.open_settings();a.setting('models');a.page_action('添加 Provider')
   assert a.diag()['view']=='provider'
   for x,y,text in [(250,120,'fixture-key'),(250,180,'fixture-model')]:
    a.tap(x,y);request=a.receive('keyboard.open')
    a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=text)
    wait_for(lambda:not a.diag()['editor']['active'])
   taprow('api_compatibility');assert a.diag()['view']=='api_compatibility'
   shot('standard');taprow('enabled');shot('compatible-top')
   for key in ('streaming','usage','summary','replay','http1'):
    b=locate(key);assert b[3]-b[1]+1==56 and b[2]-b[0]+1>=44
    old=a.diag()['settings']['rows'][key]['checked'];taprow(key)
    assert a.diag()['settings']['rows'][key]['checked']!=old
   locate('timeout');shot('compatible-bottom')
   taprow('timeout');request=a.receive('keyboard.open')
   a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='900');wait_for(lambda:not a.diag()['editor']['active'])
   a.tap(40,32);assert a.diag()['view']=='provider';shot('provider-return')
   # Save is explicit; visiting/toggling the subpage alone never mutates Provider.
   with sqlite3.connect(p/'penchat.db') as db:
    assert all('compatibility' not in json.loads(v) for v, in db.execute('select metadata_json from providers'))
   for _ in range(15):
    if a.diag()['page']['actions']['保存'][1]>=0:break
    a.settings_swipe(100);time.sleep(.2)
   a.page_action('保存')
   with sqlite3.connect(p/'penchat.db') as db:
    configs=[json.loads(v) for v, in db.execute('select metadata_json from providers')]
    c=next(v['compatibility'] for v in configs if 'compatibility' in v)
    assert c==dict(enabled=True,streaming=False,usage=False,summary=False,replay=False,http1=True,timeout=900),c
  finally:a.close()
for size in ((960,266),(936,280)):run(*size)
print('Compatibility UI checked at both logical sizes; controls, scrolling, keyboard, explicit save and metadata persistence passed.')
