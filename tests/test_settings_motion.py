#!/usr/bin/env python3
"""Settings enters from the right and exits over the retained chat pixels."""
import json,pathlib,sqlite3,tempfile,time
import test_lvgl_app as ui
from test_chat_style import seed
from image_gestures import capture
ui.port=1;out=ui.ROOT/'.work/resource-residency/settings-motion';out.mkdir(parents=True,exist_ok=True)
for w,h in [(960,266),(640,260),(568,1210)]:
 for theme in ['dark','light']:
  with tempfile.TemporaryDirectory(prefix='penchat-settings-motion-') as folder:
   seed(folder,'设置动画背景文字。\n\n'*12)
   with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:db.execute('insert into app_settings values(?,?,1)',('theme_mode',json.dumps(theme)))
   a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',folder,official_adapter=False,screen={'width':w,'height':h})
   try:
    a.open_navigation();b=a.diag()['navigation_footer']['settings']['bounds'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
    a.send('touch',x=x,y=y,pressed=True);time.sleep(.03);a.send('touch',x=x,y=y,pressed=False)
    s=ui.wait_for(lambda:(v if (v:=a.diag())['view']=='settings' else None))
    assert s['page_slide_x']>0 and s['surface_motion'],s['page_slide_x']
    capture(a,out/f'{w}-{theme}-enter.png');ui.wait_for(lambda:not a.diag()['surface_motion'])
    assert a.diag()['page_slide_x']==0
    old=a.diag()['surface_snapshot_timing']['count']
    s=a.diag()
    x=s['screen_safety']['navigation_x']+22 if h>w else 40
    y=s['safe_insets'][1]+24 if h>w else 32
    a.tap(x,y)
    assert a.diag()['surface_snapshot_timing']['count']==old+1
    capture(a,out/f'{w}-{theme}-leave.png');ui.wait_for(lambda:not a.diag()['surface_motion'])
    assert a.diag()['view']=='chat' and not a.diag()['surface_motion']
    print('Settings right-side entry/exit, snapshots retired:',w,h,theme,'passed',flush=True)
   finally:a.close()
