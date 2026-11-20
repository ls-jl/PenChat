#!/usr/bin/env python3
"""Scaled guide keeps every action reachable without enlarging illustration buffers."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from test_onboarding_helpers import action
from image_gestures import capture

t.port=1;out=t.ROOT/'.work/onboarding-scale';out.mkdir(parents=True,exist_ok=True)
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
for scale in (75,100,125,150):
    for w,h in ((960,266),(640,260),(568,1210)):
        with tempfile.TemporaryDirectory(prefix='penchat-guide-scale-') as folder:
            a=t.Native(binary,folder,seed_model=False,onboarding=True,
                       screen={'width':w,'height':h},ui_scale_percent=scale)
            try:
                for step in range(7):
                    d=a.diag()['onboarding'];assert d['step']==step and not d['render_failed']
                    actions=[b for key,b in d['actions'].items() if key!='caption']
                    for b in actions:assert 0<=b[0]<b[2]<w and 0<=b[1]<b[3]<h,(scale,w,step,b)
                    for i,b in enumerate(actions):
                        for c in actions[i+1:]:
                            assert b[2]<c[0] or c[2]<b[0] or b[3]<c[1] or c[3]<b[1],(scale,w,step,b,c)
                    if w==640 and scale==150 and step==4:
                        b=d['reader_bounds'];assert d['reader_scroll_max']>0
                        x=(b[0]+b[2])//2;start=b[3]-5;end=b[1]+5
                        a.send('touch',x=x,y=start,pressed=True)
                        for n in range(1,9):
                            a.send('touch',x=x,y=start+(end-start)*n//8,pressed=True)
                            time.sleep(.02)
                        a.send('touch',x=x,y=end,pressed=False)
                        t.wait_for(lambda:a.diag()['onboarding']['reader_scroll']>0)
                    assert 0<d['render_bytes']<=640*360*12
                    capture(a,out/f'{w}-{scale}-{step}.png')
                    action(a,'next')
                t.wait_for(lambda:a.diag()['view']=='chat')
                assert not a.read_messages()
                print('GUIDE_SCALE_BOUNDS_AND_ACTIONS',w,h,scale,flush=True)
            finally:a.close()
