#!/usr/bin/env python3
"""Viewer displays an honest preview while the original prepares; close is safe."""
import os,pathlib,tempfile,time
import test_lvgl_app as ui
from test_scanner_process import BUNDLE
from test_scanner_review_ui import press,finish,preview_tap
from image_gestures import capture
ui.port=1
os.environ.update(PENCHAT_SCANNER_BINARY=str(ui.ROOT/'build/scanner-host/penchat-scanner'),PENCHAT_SCAN_RESOURCES=str(BUNDLE/'resources/illumination'),PENCHAT_SCANNER_TEST='1',PENCHAT_TEST_SCAN_EXPORT_MS='800',LD_LIBRARY_PATH=str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread'))
for w,h in [(960,266),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-scan-preview-') as folder:
  a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   press(a);finish(a);preview_tap(a);v=a.diag()['image_viewer']
   assert v['open'] and v['preview_visible'] and not v['loaded']
   capture(a,ui.ROOT/'.work/scan-ready-latency'/('preview-loading-'+str(w)+'.png'))
   a.tap(w-38,30);ui.wait_for(lambda:not a.diag()['image_viewer']['open'])
   ui.wait_for(lambda:a.diag()['scan_latency']['decoded'])
   preview_tap(a);ui.wait_for(lambda:a.diag()['image_viewer']['loaded'])
   assert not a.diag()['image_viewer']['preview_visible'] and not a.read_messages()
   print('Immediate labelled preview, close while preparing, cached full reopen:',w,h,'passed',flush=True)
  finally:a.close()
