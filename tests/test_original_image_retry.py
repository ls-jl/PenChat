#!/usr/bin/env python3
"""A repaired scan original must be retried instead of reusing a failed decode."""
import pathlib,tempfile
import test_lvgl_app as ui
from test_image_viewer import fixture,open_photo,png
ui.port=1
for w,h in [(640,260),(960,266),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-original-retry-') as folder:
  root=pathlib.Path(folder);fixture(root,'scan');original=root/'assets/original.png';original.unlink()
  a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',root,official_adapter=False,screen={'width':w,'height':h})
  try:
   open_photo(a);ui.wait_for(lambda:a.diag()['image_viewer']['failed'])
   a.tap(w-38,30);ui.wait_for(lambda:not a.diag()['image_viewer']['open'])
   png(original,12000,32);open_photo(a)
   ui.wait_for(lambda:a.diag()['image_viewer']['loaded'])
   assert a.diag()['image_viewer']['frame']==[12000,32]
   print('Scan original decode failure -> repair -> close/reopen loads:',w,h,'passed',flush=True)
  finally:a.close()
