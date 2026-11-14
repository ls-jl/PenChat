#!/usr/bin/env python3
"""Composer scale geometry and exact 100% before/after snapshots; host only."""
import argparse,json,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture

t.port=1
FIELDS=('composer_bounds','composer_text_bounds','attachment_button','composer_radius',
        'send_diameter','send_center_x2','send_center_y2','toolbar_y','to_bottom_x','to_bottom_y')

def run(binary, output, scales, baseline=None):
    output.mkdir(parents=True,exist_ok=True)
    for scale in scales:
        for w,h in ((960,266),(640,260),(568,1210)):
            with tempfile.TemporaryDirectory(prefix='penchat-composer-scale-') as folder:
                a=t.Native(binary,folder,seed_model=False,official_adapter=False,
                           screen={'width':w,'height':h,'direction':0},
                           ui_scale_percent=None if scale==100 else scale)
                try:
                    base=None
                    for name,text in (('empty',''),('short','短消息 Hello'),('long','多行内容与 composer spacing。\n'*60)):
                        if text:
                            bounds=a.diag()['composer_text_bounds']
                            a.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
                            t.wait_for(lambda:a.diag()['editor']['active'] and bool(a.diag()['editor']['keys']))
                            # Docked placement is based on the unscaled keyboard's actual panel.
                            if h>w:
                                d=a.diag();b=d['composer_bounds']
                                assert b[3]<d['editor']['panel_top'] and b[1]>=0
                                assert d['reader_height']>=0
                            a.editor_action('official');request=a.receive('keyboard.open')
                            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=text)
                            t.wait_for(lambda:not a.diag()['editor']['active'])
                        time.sleep(.25)
                        if a.diag()['composer_hidden']:
                            r=a.diag()['composer_shortcut']['bounds'];point=((r[0]+r[2])//2,(r[1]+r[3])//2)
                            a.tap(*point);a.tap(*point)
                            t.wait_for(lambda:not a.diag()['composer_hidden'] and a.diag()['composer_bounds'][3]<h)
                            time.sleep(.25)
                        d=a.diag();b=d['composer_bounds'];entry=d['composer_text_bounds']
                        height=b[3]-b[1]+1
                        if base is None:base=height
                        assert d['width']==w and d['height']==h
                        assert 0<=b[0]<b[2]<w and 0<=b[1]<b[3]<h,(scale,w,name,b)
                        assert b[0]<=entry[0]<entry[2]<=b[2] and b[1]<=entry[1]<entry[3]<=b[3]
                        assert base<=height<=base*(3 if h>w else 2),(scale,w,name,height,base)
                        assert d['composer_radius']==base//2
                        assert not d['to_bottom_visible'],'An unsent draft has no reading position to return from'
                        assert not a.read_messages(),'Editing/scale must not send or persist drafts'
                        key=f'{w}x{h}-{scale}-{name}'
                        state={k:d[k] for k in FIELDS}
                        (output/(key+'.json')).write_text(json.dumps(state,indent=2))
                        capture(a,output/(key+'.png'))
                        if baseline:
                            assert state==json.loads((baseline/(key+'.json')).read_text()),key
                            assert (output/(key+'.png')).read_bytes()==(baseline/(key+'.png')).read_bytes(),key
                    print(f'Composer {w}x{h} {scale}%: bounds, growth, docked position and no-send passed.',flush=True)
                finally:a.close()

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--binary',type=pathlib.Path,default=t.ROOT/'build/host/penchat-lvgl')
    p.add_argument('--output',type=pathlib.Path,default=t.ROOT/'.work/composer-scale')
    p.add_argument('--scales',type=int,nargs='+',default=[75,100,125,150])
    p.add_argument('--baseline',type=pathlib.Path)
    a=p.parse_args();run(a.binary.resolve(),a.output,a.scales,a.baseline)
