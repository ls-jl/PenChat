#!/usr/bin/env python3
"""Global text preferences compose once and preserve independent UI geometry."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_font_size import fixture,slider
from ui_scale_ui import back

t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
BASE={'body':21,'button':20,'title':26,'field_title':15,'field_value':19,
      'composer':20,'detail':16,'tiny':12}

def check(a,ui,text):
    d=a.diag();f=d['reading_font']
    assert d['ui_scale']['applied']==ui and f['applied']==f['percent']==text
    assert f['size']==(21*ui*text+5000)//10000
    assert f['roles']=={key:(size*ui*text+5000)//10000 for key,size in BASE.items()}
    appearance=next(event for event in reversed(a.pending) if event.get('type')=='appearance')
    assert appearance['scale']==ui
    for key,size in {'title':28,'body':21,'caption':20}.items():
        assert appearance['metrics'][key]==(size*ui*text+5000)//10000,appearance
    for row in d['settings']['rows'].values():
        b,title,detail=row['bounds'],row['title'],row['detail']
        assert b[1]<=title[1]<=title[3]<detail[1]<=detail[3]<=b[3],row
        assert title[2]<row['indicator'][0] and detail[2]<row['indicator'][0],row
    return d

def run(w,h,ui):
    with tempfile.TemporaryDirectory(prefix='penchat-global-font-') as folder:
        p=pathlib.Path(folder);fixture(p)
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("insert into app_settings values('ui_scale_percent',?,1)",(json.dumps(ui),))
        a=t.Native(BINARY,p,official_adapter=False,screen={'width':w,'height':h})
        try:
            a.open_settings();check(a,ui,100)
            for percent in (150,75,100):
                a.setting('font_size');before=a.diag()['reading_font']['roles']
                slider(a,percent)
                assert a.diag()['reading_font']['roles']==before
                a.page_action('保存')
                t.wait_for(lambda:a.diag()['view']=='settings' and not a.diag()['reading_font']['pending'])
                check(a,ui,percent)
                with sqlite3.connect(p/'penchat.db') as db:
                    assert json.loads(db.execute("select value_json from app_settings where key='reading_font_percent'").fetchone()[0])==percent
            a.setting('font_size');slider(a,150);back(a)
            check(a,ui,100)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("create trigger reject_text_size before insert on app_settings when NEW.key='reading_font_percent' begin select raise(abort,'fixture write failure'); end")
            a.setting('font_size');slider(a,150);a.page_action('保存')
            t.wait_for(lambda:a.diag()['reading_font']['error'])
            f=a.diag()['reading_font']
            assert f['applied']==f['percent']==100 and f['draft']==150 and not f['pending']
            assert f['roles']=={key:(size*ui+50)//100 for key,size in BASE.items()}
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute('drop trigger reject_text_size')
            a.page_action('保存')
            t.wait_for(lambda:a.diag()['view']=='settings' and a.diag()['reading_font']['applied']==150)
            check(a,ui,150)
        finally:a.close()
        a=t.Native(BINARY,p,official_adapter=False,screen={'width':w,'height':h})
        try:
            a.open_settings();check(a,ui,150)
            a.setting('ui_scale')
            from ui_scale_ui import choose,save
            choose(a,100 if ui!=100 else 125);save(a,100 if ui!=100 else 125)
            check(a,100 if ui!=100 else 125,150)
        finally:a.close()
    print(f'Global text {w}x{h}, UI {ui}%: every role, preview/cancel, measured rows, durable failure/retry, restart and independent UI scale passed.',flush=True)

def deferred():
    with tempfile.TemporaryDirectory(prefix='penchat-font-focus-') as folder:
        p=pathlib.Path(folder);a=t.Native(BINARY,p,seed_model=False,official_adapter=False)
        try:
            a.open_settings();a.setting('font_size');slider(a,150)
            from ui_scale_ui import tap
            for _ in range(30):
                if a.diag()['scroll_y']<=0:break
                a.settings_swipe(120)
            bounds=a.diag()['page']['actions']['保存']
            assert bounds[1]>=0
            lock=sqlite3.connect(p/'penchat.db');lock.execute('begin immediate')
            try:
                tap(a,bounds,wait_motion=False)
                a.send('window.focus',sequence=1,focused=False);time.sleep(.1)
            finally:lock.commit();lock.close()
            t.wait_for(lambda:a.diag()['reading_font']['ready'])
            f=a.diag()['reading_font'];assert f['applied']==f['percent']==100 and f['pending']
            with sqlite3.connect(p/'penchat.db') as db:
                assert json.loads(db.execute("select value_json from app_settings where key='reading_font_percent'").fetchone()[0])==150
            for seq in range(2,8):
                a.send('window.focus',sequence=seq,focused=True);time.sleep(.05)
                if a.diag()['reading_font']['applied']==150:break
            assert a.diag()['view']=='settings' and a.diag()['reading_font']['changes']==1
            check(a,100,150)
        finally:a.close()
    print('Global text: durable acknowledgement waits for foreground input before applying once.',flush=True)

if __name__=='__main__':
    for case in ((960,266,100),(640,260,100),(568,1210,125),
                 (960,266,150),(640,260,150),(568,1210,150)):run(*case)
    deferred()
