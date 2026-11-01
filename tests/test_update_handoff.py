#!/usr/bin/env python3
"""Actual native UI -> exit/installation handoff -> restored next-process state."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_update import Node,package,future_version
from test_chat_style import seed
ROOT=t.ROOT;t.port=1
target=future_version();node=Node();node.release(target,body=package(target))
with tempfile.TemporaryDirectory(prefix='penchat-update-handoff-') as folder:
 p=pathlib.Path(folder);seed(p,'Preserve previous messages.');config=p/'update.json';settings=dict(root=f'http://localhost:{node.port}/penchat-rk/',nodes=['127.0.0.1'],auto=False);config.write_text(json.dumps(settings))
 previous=os.environ.get('PENCHAT_TEST_UPDATE_CONFIG');os.environ['PENCHAT_TEST_UPDATE_CONFIG']=str(config)
 binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
 a=None
 try:
  a=t.Native(binary,p);draft='更新后保留的未发送草稿';a.edit(draft);before=a.read_messages();session=a.diag()['session']
  a.open_settings();a.setting('update');t.wait_for(lambda:a.diag()['update']['status']=='available')
  # The UI has already accepted the update; the test bridge has no package manager.
  a.send('update.test',operation='download',args={'snapshot':{'session':session,'draft':draft,'preferences':{},'scroll':0,'offset':0,'following':True}})
  ticket=a.receive('update.install');assert ticket['appid']==json.loads((ROOT/'package.json').read_text())['appid'];assert ticket['version']==target;assert len(ticket['operation'])==36
  assert a.process.wait(timeout=8)==0;a.close();a=None
  saved=json.loads((p/'.updates/install.json').read_text());assert saved['state']=='requested';assert saved['operation']==ticket['operation']
  snapshot=json.loads((p/'.updates/restore.json').read_text())['snapshot'];assert snapshot['draft']==draft
  settings['current']=target;config.write_text(json.dumps(settings));a=t.Native(binary,p)
  t.wait_for(lambda:a.diag()['draft_length']==len(draft));assert a.diag()['session']==session;assert a.read_messages()==before
  t.wait_for(lambda:not (p/'.updates/restore.json').exists());assert not (p/'.updates/package.amr').exists()
 finally:
  if a:a.close()
  node.close()
  if previous is None:os.environ.pop('PENCHAT_TEST_UPDATE_CONFIG',None)
  else:os.environ['PENCHAT_TEST_UPDATE_CONFIG']=previous
print('Native update handoff: DB flush, clean exit, operation ticket, preserved draft/session/history and next-process restoration passed.')
