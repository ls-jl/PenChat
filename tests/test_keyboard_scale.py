#!/usr/bin/env python3
"""All keyboard modes retain their complete key set under host-only UI scale."""
from test_keyboard_design import scenario
import test_lvgl_app as t
for scale in (75,125,150):
    for fixture in ((960,266,False,False),(640,260,False,False),(640,260,True,True),(568,1210,False,False)):
        output=t.ROOT/'.work/keyboard-scale'/str(scale)
        output.mkdir(parents=True,exist_ok=True)
        scenario(t.ROOT/'build/host/penchat-lvgl',output,*fixture,scale=scale)
        print('KEYBOARD_SCALE_COMPLETE_KEYS',scale,fixture,flush=True)
