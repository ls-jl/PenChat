#!/usr/bin/env python3
"""Actual scan review footer: fixed, paired and centered at three screen shapes."""
import json,os,pathlib,signal,sqlite3,tempfile,time
import test_lvgl_app as t
from test_scanner_ui import BUNDLE,scan,scan_action
ROOT=t.ROOT;OUT=ROOT/'.work/scan-rollback-options';OUT.mkdir(exist_ok=True);(OUT/'tmp').mkdir(exist_ok=True);t.port=1

def run():
    env=dict(PENCHAT_SCANNER_BINARY=os.environ.get('PENCHAT_SCANNER_TEST_BINARY',str(ROOT/'build/scanner-host/penchat-scanner')),PENCHAT_SCAN_RESOURCES=str(BUNDLE/'resources/illumination'),PENCHAT_SCANNER_TEST='1',LD_LIBRARY_PATH=str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread'))
    keys=list(env)+['PENCHAT_SCANNER_TEST_PROFILE','PENCHAT_TEST_Y07_ROUNDED','PENCHAT_TEST_SCAN_BAD_TAIL'];old={k:os.environ.get(k) for k in keys};os.environ.update(env);rows=[]
    try:
        for w,h in [(960,266),(640,260),(568,1210)]:
            for scale in [100,150]:
                for theme in ['dark','light']:
                    os.environ['PENCHAT_SCANNER_TEST_PROFILE']='s6' if w==640 else 'orange'
                    os.environ['PENCHAT_TEST_Y07_ROUNDED']='1' if h>w else '0'
                    partial=w==640 and scale==150 and theme=='dark'
                    if partial:os.environ['PENCHAT_TEST_SCAN_BAD_TAIL']='1'
                    else:os.environ.pop('PENCHAT_TEST_SCAN_BAD_TAIL',None)
                    with tempfile.TemporaryDirectory(prefix='scan-footer-',dir=OUT/'tmp') as folder:
                        folder=pathlib.Path(folder);schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
                        with sqlite3.connect(folder/'penchat.db') as db:
                            db.executescript(schema)
                            db.execute("insert into app_settings values('theme_mode',?,1)",(json.dumps(theme),))
                            db.execute("insert into app_settings values('reading_font_percent',?,1)",(str(scale),))
                        app=t.Native(ROOT/'build/host/penchat-lvgl',folder,seed_model=False,official_adapter=False,screen={'width':w,'height':h},ui_scale_percent=scale)
                        try:
                            d=scan(app,56 if partial else 33);assert d['scan_result']['usable']
                            u,c=d['scan_actions']['use'],d['scan_actions']['cancel']
                            assert u[1]==c[1] and u[3]==c[3] and u[2]<c[0]
                            assert abs(u[0]+c[2]+1-w)<=1,(w,scale,u,c)
                            assert u[3]-u[1]+1>=44 and c[3]-c[1]+1>=44
                            assert 0<=u[0]<u[2]<w and 0<=c[0]<c[2]<w and 0<=u[1]<u[3]<h
                            preview=d['scan_preview_area']
                            assert d['reader_height']==preview[1] and preview[3]<u[1]
                            assert preview[3]-preview[1]+1>=60
                            assert bool(d['scan_warning'])==partial
                            if w>h:
                                assert preview[0]==0 and preview[2]==w-1
                                if not partial: assert preview[1]<=round(4*scale/100)
                            assert h-u[3]<=max(80,h//10)
                            image=folder/'lvgl-diagnostic.png';app.process.send_signal(signal.SIGUSR1)
                            t.wait_for(lambda:image.exists() and image.read_bytes().endswith(b'IEND\xaeB`\x82'))
                            (OUT/f'scan-{w}-{scale}-{theme}.png').write_bytes(image.read_bytes())
                            scan_action(app,'cancel');t.wait_for(lambda:app.diag()['view']=='chat')
                            if w==640 and scale==100:
                                x,y=(c[0]+c[2])//2,(c[1]+c[3])//2
                                # Start immediately after cancel, then release
                                # after the guard expires. Only a fresh tap can edit.
                                app.send('touch',x=x,y=y,pressed=True)
                                time.sleep(.36)
                                app.send('touch',x=x,y=y,pressed=False)
                                time.sleep(.06)
                                assert not app.diag()['editor']['active']
                                app.tap(x,y)
                                assert app.diag()['editor']['active'],(x,y)
                            assert not app.read_messages() and app.diag()['summary']['sessions']==0
                            rows.append(dict(width=w,height=h,scale=scale,theme=theme,partial=partial,passed=True))
                        finally:app.close()
    finally:
        for k,v in old.items():
            if v is None:os.environ.pop(k,None)
            else:os.environ[k]=v
    (OUT/'scan-footer-results.json').write_text(json.dumps(rows,indent=2)+'\n');print('SCAN_FOOTER_MATRIX_OK',len(rows))
if __name__=='__main__':run()
