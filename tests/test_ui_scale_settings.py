#!/usr/bin/env python3
"""Runtime global scale: real controls, asynchronous storage and retained state."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from ui_scale_ui import open_settings,back,choose,save,tap
from test_font_size import fixture
from image_gestures import capture

t.port=1;out=t.ROOT/'.work/ui-scale-settings/screens';out.mkdir(parents=True,exist_ok=True)
for w,h in [(960,266),(640,260),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-ui-scale-') as folder:
        folder=pathlib.Path(folder);fixture(folder)
        (folder/'.updates').mkdir();app=json.loads((t.ROOT/'package.json').read_text())
        (folder/'.updates/restore.json').write_text(json.dumps({'appid':app['appid'],'target':app['version'],'snapshot':{'session':'fixture','draft':'保留草稿','preferences':{'model_id':'a'},'scroll':0,'offset':0,'following':True}}))
        if h>w:os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False,official_adapter=False,screen={'width':w,'height':h})
        try:
            for scale in [75,125,150,100]:
                open_settings(a);assert len(a.diag()['settings']['rows'])==26
                a.setting('ui_scale');before=a.diag()['ui_scale']['applied']
                choose(a,scale);d=a.diag();assert d['ui_scale']['applied']==before and d['ui_scale']['draft']==scale
                capture(a,out/f'{w}-preview-{scale}.png')
                save(a,scale);d=a.diag();assert not d['ui_scale']['pending'] and d['ui_scale']['saved']==scale
                b=d['settings']['rows']['ui_scale']['bounds'];assert 0<=b[1]<=b[3]<h,(w,scale,b,d['settings']['viewport'])
                back(a);t.wait_for(lambda:a.diag()['view']=='chat')
                d=a.diag();assert d['draft_length']==len('保留草稿') and d['session']=='fixture'
                assert d['reading_font']['size']==(21*scale*100+5000)//10000
                capture(a,out/f'{w}-chat-{scale}.png')
                tap(a,d['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['keys'])
                e=a.diag()['editor'];assert e['requested_scale']==scale and min(100,scale)<=e['effective_scale']<=scale
                a.editor_action('done');t.wait_for(lambda:not a.diag()['editor']['active'])
                print('RUNTIME_SCALE_STATE_AND_KEYBOARD',w,h,scale,flush=True)
            open_settings(a);a.setting('ui_scale');choose(a,117);back(a)
            assert a.diag()['ui_scale']['applied']==100
            a.setting('ui_scale');choose(a,117);save(a,117)
            a.setting('ui_scale');a.page_action('恢复默认');assert a.diag()['ui_scale']['draft']==100
            back(a);assert a.diag()['ui_scale']['applied']==117
            # A failed durable write must leave applied and saved values alone.
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute("create trigger reject_scale before insert on app_settings when NEW.key='ui_scale_percent' begin select raise(abort,'fixture write failure'); end")
            a.setting('ui_scale');choose(a,125);a.page_action('保存')
            t.wait_for(lambda:a.diag()['ui_scale']['error']);d=a.diag()['ui_scale']
            assert d['applied']==117 and d['saved']==117 and d['draft']==125 and not d['pending']
            with sqlite3.connect(folder/'penchat.db') as db:db.execute('drop trigger reject_scale')
            save(a,125)
        finally:a.close();os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False,screen={'width':w,'height':h})
        try:assert a.diag()['ui_scale']['applied']==125
        finally:a.close()
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False,screen={'width':w,'height':h},ui_scale_percent=75)
        try:assert a.diag()['ui_scale']['applied']==75 and a.diag()['ui_scale']['saved']==125
        finally:a.close()
        with sqlite3.connect(folder/'penchat.db') as db:assert json.loads(db.execute("select value_json from app_settings where key='ui_scale_percent'").fetchone()[0])==125
        print('SCALE_CANCEL_RESET_FAILURE_RESTART_AND_HOST_OVERRIDE',w,h,flush=True)
