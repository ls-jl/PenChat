#!/usr/bin/env python3
"""Scaled rich-text menus, editor caret dragging and safe magnifier bounds."""
import pathlib,tempfile,time
import test_lvgl_app as t
from test_text_selection import fixture,bounds_tap
from image_gestures import capture

t.port=1;out=t.ROOT/'.work/selection-scale';out.mkdir(parents=True,exist_ok=True)
for scale in (75,125,150):
    for w,h in ((960,266),(640,260),(568,1210)):
        with tempfile.TemporaryDirectory(prefix='penchat-selection-scale-') as folder:
            folder=pathlib.Path(folder);fixture(folder)
            a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,official_adapter=False,
                       screen={'width':w,'height':h},ui_scale_percent=scale)
            try:
                region=next(r for r in a.diag()['text_selection']['regions'] if r['key']=='a')
                b=region['parts'][0];a.long_press(b[0]+round(14*scale/100),b[1]+round(12*scale/100),.66)
                t.wait_for(lambda:a.diag()['text_selection']['menu'])
                menu=a.diag()['text_selection']['menu_bounds']
                assert 0<=menu[0]<menu[2]<w and 0<=menu[1]<menu[3]<h
                capture(a,out/f'{w}-{scale}-reader.png')
                a.tap(w-2,h//2)
                bounds_tap(a,a.diag()['composer_bounds']);t.wait_for(lambda:a.diag()['editor']['active'])
                a.editor_action('official');request=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='Alpha bravo 世界')
                t.wait_for(lambda:not a.diag()['editor']['active'])
                bounds_tap(a,a.diag()['composer_bounds']);t.wait_for(lambda:a.diag()['editor']['active'])
                if w==640:a.editor_action('compact-view')
                t.wait_for(lambda:not a.diag().get('surface_motion',False))
                e=a.diag()['editor'];b=e['text_bounds'];space=e['coordinate_space']
                sx=round(256*space['native_width']/space['width'])/256 if space['scaled'] else 1
                sy=round(256*space['native_height']/space['height'])/256 if space['scaled'] else 1
                x,y=round((b[0]+12)*sx),round((b[1]+12)*sy)
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.66)
                a.send('touch',x=x+round(26*scale/100),y=y,pressed=True)
                t.wait_for(lambda:a.diag()['editor']['magnifier'])
                for x in (0,w-1):
                    a.send('touch',x=x,y=y,pressed=True);time.sleep(.08)
                    e=a.diag()['editor'];lens=e['magnifier_bounds']
                    assert e['cursor_drag']
                    assert round(lens[0]*sx)>=0 and round(lens[1]*sy)>=0
                    assert round(lens[2]*sx)<w and round(lens[3]*sy)<h,(scale,w,lens,sx,sy)
                assert a.diag()['editor']['cursor']==len('Alpha bravo 世界')
                capture(a,out/f'{w}-{scale}-magnifier.png')
                a.send('touch',x=x,y=y,pressed=False,cancelled=True)
                t.wait_for(lambda:not a.diag()['editor']['magnifier'])
                print('SELECTION_SCALE_COORDINATES_AND_CARET',w,h,scale,flush=True)
            finally:a.close()
