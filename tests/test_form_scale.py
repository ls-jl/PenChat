#!/usr/bin/env python3
"""Settings and provider/model form geometry; exact identity-scale comparison."""
import argparse,json,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from provider_ui import activate
from image_gestures import capture

t.port=1

def run(binary,output,scales,baseline=None):
    output.mkdir(parents=True,exist_ok=True)
    for scale in scales:
        for w,h in ((960,266),(640,260),(568,1210)):
            with tempfile.TemporaryDirectory(prefix='penchat-form-scale-') as folder:
                seed(folder,1)
                a=t.Native(binary,folder,official_adapter=False,screen={'width':w,'height':h},
                           ui_scale_percent=None if scale==100 else scale)
                def back():
                    d=a.diag()
                    if h>w:
                        # Return the scrolling page header to view before tapping.
                        for _ in range(30):
                            if a.diag()['scroll_y']<=0:break
                            a.settings_swipe(h//2)
                        d=a.diag();head=d['page']['heading']
                        a.tap(d['screen_safety']['navigation_x']+round(22*scale/100),head[1]+round(17*scale/100))
                    else:a.tap(round(40*scale/100),round(32*scale/100))
                def record(name):
                    # Capture a stable top-of-page, not animated scroll positions.
                    for _ in range(30):
                        if a.diag()['scroll_y']<=0:break
                        a.settings_swipe(h//2)
                    time.sleep(.25);d=a.diag()
                    state={k:d[k] for k in ('view','page','settings')}
                    key=f'{w}x{h}-{scale}-{name}'
                    (output/(key+'.json')).write_text(json.dumps(state,ensure_ascii=False,indent=2))
                    capture(a,output/(key+'.png'))
                    if baseline:
                        assert state==json.loads((baseline/(key+'.json')).read_text()),key
                        assert (output/(key+'.png')).read_bytes()==(baseline/(key+'.png')).read_bytes(),key
                    return d
                try:
                    a.open_settings()
                    d=record('settings')
                    groups=d['settings']['groups'];rows=d['settings']['rows']
                    for row in rows.values():
                        b=row['bounds'];assert b[0]>=0 and b[2]<w
                        assert row['title'][3]<row['detail'][1]<=row['detail'][3]<=b[3]
                        assert row['title'][2]<row['indicator'][0]
                    a.setting('models');record('providers')
                    activate(a,'provider-open:p');record('provider')
                    activate(a,'provider_advanced');record('provider-advanced')
                    activate(a,'api_compatibility');record('compatibility');back()
                    activate(a,'model-edit:a');record('model')
                    activate(a,'参数映射');record('model-advanced')
                    activate(a,'完成');activate(a,'provider-add-models');record('model-list')
                    # Layout traversal must not commit the provider workspace.
                    with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:
                        assert db.execute("select count(*) from model_configs").fetchone()[0]==2
                        assert db.execute("select name from providers where id='p'").fetchone()[0]=='Fixture'
                    print(f'Forms {w}x{h} {scale}%: eight pages and staged workspace passed.',flush=True)
                finally:a.close()

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--binary',type=pathlib.Path,default=t.ROOT/'build/host/penchat-lvgl')
    p.add_argument('--output',type=pathlib.Path,default=t.ROOT/'.work/form-scale')
    p.add_argument('--scales',type=int,nargs='+',default=[75,100,125,150])
    p.add_argument('--baseline',type=pathlib.Path)
    args=p.parse_args();run(args.binary.resolve(),args.output,args.scales,args.baseline)
