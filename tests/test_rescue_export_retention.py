#!/usr/bin/env python3
"""Actual App shutdown preserves public exports only for its rescue handoff."""
import json,os,pathlib,tempfile,time
import test_lvgl_app as ui
ui.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl'))
for kind in ['owned','expired','other-pid','normal']:
 with tempfile.TemporaryDirectory(prefix='penchat-rescue-export-') as temp:
  folder=pathlib.Path(temp);data=folder/'data';root=folder/'public';old=os.environ.get('PENCHAT_TRANSFER_ROOT');os.environ['PENCHAT_TRANSFER_ROOT']=str(root)
  app=ui.Native(binary,data)
  try:
   ui.wait_for(lambda:(root/'导出/聊天').is_dir());export=root/'导出/聊天/fixture.txt';export.write_text('rescue fixture')
   incoming=root/'导入/聊天/fixture.txt';incoming.write_text('keep import')
   if kind!='normal':
    marker=dict(pid=app.process.pid+(1 if kind=='other-pid' else 0),time_ms=int(time.time()*1000)-(360000 if kind=='expired' else 0))
    (data/'.rescue-preserve-public').write_text(json.dumps(marker))
  finally:
   app.close()
   if old is None:os.environ.pop('PENCHAT_TRANSFER_ROOT',None)
   else:os.environ['PENCHAT_TRANSFER_ROOT']=old
  assert export.exists()==(kind=='owned'),kind
  assert incoming.read_text()=='keep import'
print('RESCUE_EXPORT_HANDOFF_OWN_PID_TTL_AND_NORMAL_EXIT_OK')
