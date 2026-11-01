#!/usr/bin/env python3
"""Native LVGL update screens, idle gating, notes pagination and cancellation."""
import json,os,pathlib,signal,tempfile,time
import test_lvgl_app as t
from test_update import Node,package
ROOT=t.ROOT;OUT=ROOT/'.work/update-ui';OUT.mkdir(parents=True,exist_ok=True);t.port=1
for w,h in [(960,266),(936,280)]:
 node=Node();body=node.release('0.2.22',body=package('0.2.22',size=5*1024*1024),sha=False,note=('## 更新说明\n\n改进下载与恢复。\n\n'*700).encode());node.rate=100000
 with tempfile.TemporaryDirectory(prefix='penchat-update-ui-') as folder:
  p=pathlib.Path(folder);config=p/'update-config.json';config.write_text(json.dumps(dict(root=f'http://localhost:{node.port}/penchat/',nodes=['127.0.0.1'],auto=False)))
  previous=os.environ.get('PENCHAT_TEST_UPDATE_CONFIG');os.environ['PENCHAT_TEST_UPDATE_CONFIG']=str(config)
  a=t.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl')),p,screen={'width':w,'height':h,'direction':0,'tp_direction':0,'dbuffer':True})
  def shot(name):
   path=p/'lvgl-diagnostic.png'
   if path.exists():path.unlink()
   a.process.send_signal(signal.SIGUSR1);t.wait_for(path.exists);(OUT/f'{w}-{h}-{name}.png').write_bytes(path.read_bytes())
  def action(name):
   for _ in range(30):
    d=a.diag();b=d['update']['actions'].get(name)
    if not b:time.sleep(.1);continue
    if d['page']['scrolling']:time.sleep(.1);continue
    if b[1]>=0 and b[3]<h:a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);return
    a.settings_swipe(120 if b[1]<0 else -120);time.sleep(.15)
   raise AssertionError((name,a.diag()))
  try:
   a.open_settings();a.setting('update');t.wait_for(lambda:a.diag()['update']['status']=='available');shot('missing-checksum')
   assert a.diag()['update']['weak'];action('page_next');t.wait_for(lambda:a.diag()['update']['note_page']==1);shot('notes-page2');a.settings_swipe(-180);time.sleep(.25);shot('notes-content')
   action('download');t.wait_for(lambda:a.diag()['update']['status']=='downloading');time.sleep(.35);shot('downloading')
   action('cancel');t.wait_for(lambda:a.diag()['view']=='settings');assert not (p/'.updates/download.part').exists()
   a.setting('update');t.wait_for(lambda:a.diag()['update']['status']=='available');action('later');t.wait_for(lambda:a.diag()['view']=='settings');shot('settings')
   # Automatic checks never interrupt an active native editor.
   a.to_chat();time.sleep(.2);a.tap(w//2,h-33);t.wait_for(lambda:a.diag()['editor']['active'])
   node.release('0.2.23');a.send('update.test',operation='check',args={'manual':False});t.wait_for(lambda:a.diag()['update']['status']=='available');assert a.diag()['editor']['active'];assert a.diag()['view']=='chat'
   a.send('visibility',visible=False);a.send('visibility',visible=True);assert a.diag()['editor']['active'];shot('editor-uninterrupted')
  finally:
   a.close();node.close()
   if previous is None:os.environ.pop('PENCHAT_TEST_UPDATE_CONFIG',None)
   else:os.environ['PENCHAT_TEST_UPDATE_CONFIG']=previous
print('Update screens: both sizes, checksum consent, notes pages, cancellation, manual/automatic and editor isolation passed.')
# Include the real exit/restart handoff in the regular update UI regression.
import test_update_handoff
