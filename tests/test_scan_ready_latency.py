#!/usr/bin/env python3
"""Real worker/LVGL: prepare and decode before click, persist only on confirmation."""
import json,os,pathlib,tempfile,time
import test_lvgl_app as ui
from test_scanner_process import BUNDLE
from test_scanner_review_ui import press,finish,preview_tap
ui.port=1
os.environ.update(PENCHAT_SCANNER_BINARY=os.environ.get('PENCHAT_SCANNER_TEST_BINARY',str(ui.ROOT/'build/scanner-host/penchat-scanner')),PENCHAT_SCAN_RESOURCES=str(BUNDLE/'resources/illumination'),PENCHAT_SCANNER_TEST='1',LD_LIBRARY_PATH=str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread'))
rows=[]
for w,h in [(960,266),(640,260),(568,1210)]:
 os.environ['PENCHAT_SCANNER_TEST_PROFILE']='s6' if w==640 else 'orange'
 with tempfile.TemporaryDirectory(prefix='penchat-scan-latency-') as folder:
  a=ui.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl')),folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   press(a);finish(a)
   ready=ui.wait_for(lambda:(s if (s:=a.diag())['scan_latency']['decoded'] else None))
   assert not a.read_messages() and ready['summary']['sessions']==0
   latency=ready['scan_latency'];assert latency['finished']<=latency['prepared']<=latency['decoded']
   generation=ready['scanner']['generation'];preview_tap(a)
   s=ui.wait_for(lambda:(s if (s:=a.diag())['image_viewer'].get('loaded') else None))
   view=s['scan_latency']['view_loaded']-s['scan_latency']['view_clicked'];assert view<100,(w,h,view)
   a.tap(w-38,30);ui.wait_for(lambda:not a.diag()['image_viewer']['open'])
   # Retain originals through the save modal and commit exactly once.
   b=a.diag()['scan_actions']['use'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   ui.wait_for(lambda:a.diag()['view']=='chat');s=a.diag()
   assert not a.read_messages() and s['summary']['sessions']==0
   with __import__('sqlite3').connect(pathlib.Path(folder)/'penchat.db') as db:
    assert db.execute('select count(*) from draft_attachments').fetchone()[0]==1
   assert s['scanner']['generation']==generation
   saved=s['scan_latency'];assert saved['stored']>=saved['confirm_clicked']>0
   rows.append({'geometry':[w,h],'prepare_after_finished_ms':latency['prepared']-latency['finished'],'export_us':latency['export_us'],'decode_after_prepare_ms':latency['decoded']-latency['prepared'],'view_click_to_loaded_ms':view,'confirm_to_saved_ms':saved['stored']-saved['confirm_clicked']})
  finally:a.close()
print(json.dumps(rows),flush=True)
