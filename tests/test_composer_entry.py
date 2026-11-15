#!/usr/bin/env python3
"""The existing portrait composer rises with the docked keyboard."""
import pathlib,tempfile,time
import test_lvgl_app as ui
ui.port=1
for width,height in [(568,1210),(600,1280)]:
 with tempfile.TemporaryDirectory(prefix='composer-entry-') as folder:
  a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',pathlib.Path(folder),official_adapter=False,screen={'width':width,'height':height})
  try:
   before=a.diag();b=before['composer_text_bounds'];x=(b[0]+b[2])//2;y=(b[1]+b[3])//2
   a.send('touch',x=x,y=y,pressed=True);time.sleep(.025);a.send('touch',x=x,y=y,pressed=False)
   samples=[];deadline=time.monotonic()+2
   while time.monotonic()<deadline:
    s=a.diag()
    if s['editor']['active'] and s['editor']['keys']:
     samples.append((s['composer_bounds'][3],s.get('surface_motion',False)))
     if not s.get('surface_motion',False):break
    time.sleep(.01)
   assert len(samples)>1 and any(m for _,m in samples),samples
   assert samples[0][0]<=before['composer_bounds'][3]+1,samples
   assert samples[0][0]>samples[-1][0]+5,samples
   assert all(b<=a+1 for (a,_),(b,_) in zip(samples,samples[1:])),samples
   assert samples[-1][0]<before['composer_bounds'][3]
   print(width,height,'COMPOSER_RISES_CONTINUOUSLY_WITH_KEYBOARD',samples)
  finally:a.close()
