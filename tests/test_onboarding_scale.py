#!/usr/bin/env python3
"""Scaled guide keeps every action reachable without enlarging illustration buffers."""
import pathlib,tempfile
import test_lvgl_app as t
from test_onboarding_helpers import action
from image_gestures import capture

t.port=1;out=t.ROOT/'.work/onboarding-scale';out.mkdir(parents=True,exist_ok=True)
for scale in (75,125,150):
    for w,h in ((960,266),(640,260),(568,1210)):
        with tempfile.TemporaryDirectory(prefix='penchat-guide-scale-') as folder:
            a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False,onboarding=True,
                       screen={'width':w,'height':h},ui_scale_percent=scale)
            try:
                for step in range(7):
                    d=a.diag()['onboarding'];assert d['step']==step and not d['render_failed']
                    actions=[b for key,b in d['actions'].items() if key!='caption']
                    for b in actions:assert 0<=b[0]<b[2]<w and 0<=b[1]<b[3]<h,(scale,w,step,b)
                    for i,b in enumerate(actions):
                        for c in actions[i+1:]:
                            assert b[2]<c[0] or c[2]<b[0] or b[3]<c[1] or c[3]<b[1],(scale,w,step,b,c)
                    assert 0<d['render_bytes']<=640*360*12
                    capture(a,out/f'{w}-{scale}-{step}.png')
                    action(a,'next')
                t.wait_for(lambda:a.diag()['view']=='chat')
                assert not a.read_messages()
                print('GUIDE_SCALE_BOUNDS_AND_ACTIONS',w,h,scale,flush=True)
            finally:a.close()
