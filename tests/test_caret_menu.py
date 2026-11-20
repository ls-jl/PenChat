#!/usr/bin/env python3
"""An empty caret exposes Paste with a single tap on each native keyboard layout."""
import pathlib,tempfile,time
import test_lvgl_app as ui
ui.port=1
for width,height in [(960,266),(936,280),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='caret-menu-') as tmp:
  a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',pathlib.Path(tmp),screen={'width':width,'height':height})
  try:
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   ui.wait_for(lambda:a.diag()['editor']['active']);ui.wait_for(lambda:not a.diag().get('surface_motion',False))
   e=a.diag()['editor'];b=e['text_bounds'];c=e['coordinate_space'];x=(b[0]+5)*width/c['width'];y=(b[1]+8)*height/c['height']
   a.tap(x,y);e=a.diag()['editor'];assert e['menu'] and not e['selected'],(width,e)
   assert all(k in e['actions'] for k in ['all','copy','paste','cut','undo'])
   assert e['length']==0
   print(f'EMPTY_CARET_SINGLE_TAP_MENU_OK {width}x{height}',flush=True)
  finally:a.close()
