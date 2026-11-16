#!/usr/bin/env python3
"""Warmup/handoff is intentionally slow; native keyboard must appear first."""
import argparse,json,os,pathlib,tempfile,time
import test_lvgl_app as ui
from test_scanner_process import BUNDLE
ui.port=1
p=argparse.ArgumentParser();p.add_argument('--binary',type=pathlib.Path,default=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl')));p.add_argument('--baseline',action='store_true');args=p.parse_args()
os.environ.update(PENCHAT_SCANNER_BINARY=os.environ.get('PENCHAT_SCANNER_TEST_BINARY',str(ui.ROOT/'build/scanner-host/penchat-scanner')),PENCHAT_SCAN_RESOURCES=str(BUNDLE/'resources/illumination'),PENCHAT_SCANNER_TEST='1',PENCHAT_TEST_SCAN_INFER_MS='500',LD_LIBRARY_PATH=str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread'))
results=[]
for w,h in [(960,266),(640,260)]:
 with tempfile.TemporaryDirectory(prefix='penchat-async-keyboard-') as folder:
  a=ui.Native(args.binary,folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   ui.wait_for(lambda:a.diag()['scanner'].get('type') in ['preparing','warming'])
   b=a.diag()['composer_text_bounds'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
   a.send('touch',x=x,y=y,pressed=True);time.sleep(.025);start=time.monotonic();a.send('touch',x=x,y=y,pressed=False)
   e=ui.wait_for(lambda:(s['editor'] if (s:=a.diag())['editor']['keys'] else None))
   elapsed=round((time.monotonic()-start)*1000,2)
   if not args.baseline:
    assert elapsed<300,(elapsed,e['timing'])
    assert e['timing']['animation'] and not e['timing']['hardware_ready']
   results.append({'geometry':[w,h],'slow_handoff_ms':500,'click_to_keys_observed_ms':elapsed,'baseline':args.baseline})
  finally:a.close()
print(json.dumps(results),flush=True)
