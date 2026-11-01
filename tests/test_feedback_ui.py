#!/usr/bin/env python3
import json,os,pathlib,signal,tempfile,time
import test_lvgl_app as t
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/feedback-ui';OUT.mkdir(parents=True,exist_ok=True);t.port=1
for w,h in [(960,266),(936,280)]:
 with tempfile.TemporaryDirectory(prefix='penchat-feedback-ui-') as folder:
  p=pathlib.Path(folder);config=p/'feedback-config.json';config.write_text('{}')
  previous=os.environ.get('PENCHAT_TEST_FEEDBACK_CONFIG');os.environ['PENCHAT_TEST_FEEDBACK_CONFIG']=str(config)
  a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0,'tp_direction':0,'dbuffer':True})
  def shot(name):
   path=p/'lvgl-diagnostic.png'
   if path.exists():path.unlink()
   a.process.send_signal(signal.SIGUSR1);t.wait_for(path.exists)
   (OUT/f'{w}-{h}-{name}.png').write_bytes(path.read_bytes())
  def action(name):
   for _ in range(25):
    d=a.diag();b=d['feedback']['actions'][name]
    if d['page']['scrolling']:time.sleep(.15);continue
    if b[1]>=0 and b[3]<h:a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);return
    a.settings_swipe(120 if b[1]<0 else -120);time.sleep(.15)
   raise AssertionError((name,a.diag()))
  def request(op,**args):
   a.send('feedback.test',operation=op,args=args);time.sleep(.25)
  try:
   a.open_settings();a.setting('feedback');t.wait_for(lambda:a.diag()['view']=='feedback');shot('form')
   action('description');keyboard=a.receive('keyboard.open')
   a.send('keyboard.result',requestId=keyboard['requestId'],confirmed=True,text='偶发扫描中断，请协助检查。')
   t.wait_for(lambda:not a.diag()['keyboard']);time.sleep(.2);shot('description')
   request('draft',adapt=True)
   action('adapt');shot('adapt')
   action('start_scan');t.wait_for(lambda:a.diag()['feedback']['collecting']);shot('listening')
   # Completion is injected only into host builds; real device coverage uses evdev.
   draft=json.loads((p/'.feedback/draft.json').read_text());assert draft['adapt']
   action('skip_scan');t.wait_for(lambda:not a.diag()['feedback']['collecting'])
   action('start_home');t.wait_for(lambda:a.diag()['feedback']['collecting'])
   a.send('visibility',visible=False);time.sleep(.15)
   a.send('visibility',visible=True);time.sleep(.15)
   action('skip_home');t.wait_for(lambda:not a.diag()['feedback']['collecting']);shot('review')
   action('review');action('preview');t.wait_for(lambda:a.diag()['feedback']['screen']=='preview');shot('preview')
   for _ in range(4):a.settings_swipe(-120);time.sleep(.12)
   shot('preview-scrolled')
   report=''.join([]) # Report is deliberately absent from general UI diagnostics.
   assert 'description' not in a.diag()['feedback']
   a.tap(40,32);t.wait_for(lambda:a.diag()['feedback']['screen']=='form')
   action('records');shot('records')
   a.tap(40,32);a.tap(40,32);t.wait_for(lambda:a.diag()['view']=='settings')
   assert a.diag()['scroll_y']>0
  finally:
   a.close()
   if previous is None:os.environ.pop('PENCHAT_TEST_FEEDBACK_CONFIG',None)
   else:os.environ['PENCHAT_TEST_FEEDBACK_CONFIG']=previous
# An explicit failure stays visible rather than disappearing in the next
# asynchronous page rebuild. Uses a loopback server, never the real service.
import http.server,threading
from test_feedback import Server
Server.statuses=[401];Server.requests=[];Server.reply=b'PRIVATE_SERVER_BODY';Server.delay=0;Server.retry_after=None
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
threading.Thread(target=server.serve_forever,daemon=True).start()
w,h=960,266
try:
 with tempfile.TemporaryDirectory(prefix='penchat-feedback-error-ui-') as folder:
  p=pathlib.Path(folder);config=p/'fake-config.json';config.write_text(json.dumps({'endpoint':f'http://127.0.0.1:{server.server_port}/logs','token':'FAKE_FEEDBACK_TOKEN'}))
  previous=os.environ.get('PENCHAT_TEST_FEEDBACK_CONFIG');os.environ['PENCHAT_TEST_FEEDBACK_CONFIG']=str(config)
  a=t.Native(BINARY,p)
  try:
   a.open_settings();a.setting('feedback');action('preview');t.wait_for(lambda:a.diag()['feedback']['screen']=='preview')
   action('send');t.wait_for(lambda:a.diag()['modal']);time.sleep(.5);assert a.diag()['modal'];shot('error-notice')
   a.tap(890,222);t.wait_for(lambda:not a.diag()['modal']);shot('pending-record')
   assert a.diag()['feedback']['pending']==1;assert len(Server.requests)==1
   assert 'PRIVATE_SERVER_BODY' not in json.dumps(a.diag()['feedback'])
   a.tap(40,32);a.tap(40,32);t.wait_for(lambda:a.diag()['view']=='settings')
   ident=next((p/'.feedback/reports').glob('*.json')).stem
   Server.reply=b'{"ok":true}';Server.statuses=[201]
   request('retry',id=ident);t.wait_for(lambda:a.diag()['feedback']['pending']==0);shot('settings-count-updated')
  finally:
   a.close()
   if previous is None:os.environ.pop('PENCHAT_TEST_FEEDBACK_CONFIG',None)
   else:os.environ['PENCHAT_TEST_FEEDBACK_CONFIG']=previous
finally:server.shutdown();server.server_close()
