#!/usr/bin/env python3
"""Actual LVGL camera controls and three-layout visual smoke checks; no hardware/API."""
import pathlib,tempfile,time,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from test_lvgl_app import Native,ROOT,wait_for
from image_gestures import capture
import test_lvgl_app as transport
transport.port=1
out=ROOT/'.work/camera-layout'
for width,height in [(568,1210),(960,266),(936,280),(640,260)]:
 with tempfile.TemporaryDirectory(prefix='penchat-camera-layout-') as folder:
  a=Native(ROOT/'build/host/penchat-lvgl',folder,screen={'width':width,'height':height},seed_model=False,official_adapter=False)
  try:
   s=a.diag();b=s['composer_bounds'];a.tap(b[0]+(47 if height>width else 22),s['send_center_y2']//2)
   wait_for(lambda:a.diag()['attachments']);time.sleep(.25);b=a.diag()['attachment_actions']['camera'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   wait_for(lambda:a.diag()['view']=='camera');time.sleep(.2)
   def point(name):
    s=a.diag();b=s['camera_controls'][name];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
    if width>=height:
     x,y=x-width/2,y-height/2;angle=s['camera_angle']
     if angle==90:x,y=y,-x
     elif angle==180:x,y=-x,-y
     elif angle==270:x,y=-y,x
     return round(x+width/2),round(y+height/2)
    return x,y
   capture(a,out/f'{width}-auto.png')
   a.tap(*point('direction'));wait_for(lambda:not a.diag()['camera_auto'])
   capture(a,out/f'{width}-locked.png')
   a.tap(*point('turn'));wait_for(lambda:a.diag()['camera_angle']==90)
   capture(a,out/f'{width}-quarter.png')
   if height>width:
    c=a.diag()['camera_controls'];assert c['direction'][2]<c['shutter'][0]<c['turn'][0]
    assert abs((c['direction'][1]+c['direction'][3])-(c['turn'][1]+c['turn'][3]))<=1
   a.tap(*point('direction'));wait_for(lambda:a.diag()['camera_auto'])
   assert not a.read_messages()
   print(width,height,'auto/lock/turn controls and screenshots passed',flush=True)
  finally:a.close()
