#!/usr/bin/env python3
"""OCR choice persists independently of delayed or temporarily missing capability."""
import os,pathlib,tempfile,sqlite3,json,time
import test_lvgl_app as t
t.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
for w,h in [(960,266),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-ocr-mode-') as folder:
  def open_native(cap):
   a=t.Native(binary,folder,official_adapter=False,screen={'width':w,'height':h},ocr_capabilities=cap)
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['ocr_active'])
   return a
  a=open_native({'single':True,'multi':True})
  try:
   def state():return a.diag()['editor']
   assert not state()['ocr_multiline_preferred']
   if state()['compact_full']:a.editor_action('compact-view')
   a.editor_action('ocr-mode')
   t.wait_for(lambda:state()['ocr_multi'] and not state()['ocr_mode_save_pending'])
   with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:assert json.loads(db.execute("select value_json from app_settings where key='ocr_multiline'").fetchone()[0]) is True
   a.editor_action('done');t.wait_for(lambda:not state()['active'])
  finally:a.close()
  a=open_native({'single':True,'multi':False})
  try:
   e=a.diag()['editor'];assert e['ocr_multiline_preferred'] and not e['ocr_multi']
   request=a.receive('ocr.start')
   while request['requestId']!=a.diag()['editor']['ocr_request']:request=a.receive('ocr.start')
   a.send('ocr.capabilities',requestId=request['requestId'],session=request['session'],single=True,multi=True)
   t.wait_for(lambda:a.diag()['editor']['ocr_multi'])
   a.editor_action('done')
  finally:a.close()
  a=open_native({'single':True,'multi':True})
  try:
   assert a.diag()['editor']['ocr_multi'];a.editor_action('done')
  finally:a.close()
 print('OCR_MODE_PERSIST_REOPEN_CAPABILITY_FALLBACK_RESTORE',w,h,flush=True)

# A failed asynchronous preference save keeps this runtime's explicit choice.
with tempfile.TemporaryDirectory(prefix='penchat-ocr-mode-failure-') as folder:
 a=t.Native(binary,folder,official_adapter=False,ocr_capabilities={'single':True,'multi':True})
 try:
  with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:
   db.execute("CREATE TRIGGER reject_mode BEFORE INSERT ON app_settings WHEN NEW.key='ocr_multiline' BEGIN SELECT RAISE(FAIL,'fixture'); END")
  b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
  t.wait_for(lambda:a.diag()['editor']['ocr_active'])
  a.editor_action('ocr-mode')
  t.wait_for(lambda:a.diag()['editor']['ocr_mode_save_error'])
  assert a.diag()['editor']['ocr_multi'] and a.diag()['editor']['ocr_multiline_preferred']
  a.editor_action('done')
 finally:a.close()
print('OCR_MODE_ASYNC_SAVE_FAILURE_RETAINS_PROCESS_CHOICE',flush=True)

with tempfile.TemporaryDirectory(prefix='penchat-ocr-mode-invalid-') as folder:
 a=t.Native(binary,folder,official_adapter=False)
 a.close()
 for value in ['multi',1,None,{},[]]:
  with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:
   db.execute("insert or replace into app_settings values('ocr_multiline',?,1)",(json.dumps(value),))
  a=t.Native(binary,folder,official_adapter=False)
  try:assert not a.diag()['editor']['ocr_multiline_preferred']
  finally:a.close()
print('OCR_MODE_INVALID_STORED_TYPES_DEFAULT_SINGLE',flush=True)
