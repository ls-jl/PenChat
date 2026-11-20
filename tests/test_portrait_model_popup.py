#!/usr/bin/env python3
"""Content-sized portrait model popup, local scrolling and retained editing."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
OUT=t.ROOT/'.work/y07-model-popup/screenshots';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def tap(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def open_models(a):
    tap(a,a.diag()['attachment_button'])
    t.wait_for(lambda:a.diag()['popup']=='attachments')
    a.attachment_action('models');t.wait_for(lambda:a.diag()['popup']=='models')
    return a.diag()['popup_bounds']

def key(a,char):
    k=next(k for k in a.diag()['editor']['keys'] if k['key']==char);a.tap(k['x'],k['y'])

os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
try:
    for count in [0,2,12]:
        with tempfile.TemporaryDirectory(prefix='penchat-portrait-models-') as folder:
            p=pathlib.Path(folder);seed(p,1)
            with sqlite3.connect(p/'penchat.db') as db:
                if count==0:db.execute('delete from model_configs')
                for i in range(2,count):
                    db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,created_at,updated_at) values(?,'p',?,?,'chat',1,1)",(f'm{i:02d}',f'model-{i:02d}','模型 '+str(i)))
            a=t.Native(t.ROOT/'build/host/penchat-lvgl',p,seed_model=False,official_adapter=False,screen={'width':568,'height':1210,'direction':0})
            try:
                b=open_models(a);d=a.diag()
                assert b[3]-b[1]+1<=360 and b[2]-b[0]+1<=440 and not d['editor']['active'],b
                assert b[3]-b[1]+1<150 if count<3 else b[3]-b[1]+1==360,b
                capture(a,OUT/f'closed-{count}.png')
                if count==0:
                    a.tap(b[0]+100,b[1]+24);t.wait_for(lambda:a.diag()['view']=='provider')
                    print('Portrait empty model menu passed',flush=True)
                    continue
                # Last item is reachable by scrolling this popup, not the chat.
                before=d['scroll_y']
                if count>2:
                    x=(b[0]+b[2])//2
                    for _ in range(4):
                        for y in range(b[3]-30,b[1]+20,-30):a.send('touch',x=x,y=y,pressed=True);time.sleep(.02)
                        time.sleep(.15);a.send('touch',x=x,y=b[1]+30,pressed=False);time.sleep(.2)
                    assert a.diag()['scroll_y']==before
                    capture(a,OUT/'scrolled-last.png')
                    a.tap(b[0]+100,b[3]-24);t.wait_for(lambda:a.diag()['popup']=='')
                    assert a.diag()['active_model']=='m11',a.diag()['active_model']
                else:
                    a.tap(540,500);assert not a.diag()['attachments']
                    a.open_settings()
                    a.setting('title_model');d=a.diag();b=d['popup_bounds']
                    assert d['popup']=='title_models' and b[3]-b[1]+1<=360
                    capture(a,OUT/'title-models.png')
                    a.tap(b[0]+90,b[1]+24);assert not a.diag()['attachments']
                    a.to_chat()
                tap(a,a.diag()['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active'])
                key(a,'n');key(a,'i');t.wait_for(lambda:not a.diag()['editor']['pending'])
                before=a.diag()['editor'];b=open_models(a);after=a.diag()['editor']
                for name in ['session','preedit_length','ocr_request','build_count','keys']:assert after[name]==before[name],name
                assert b[3]-b[1]+1<=360
                key_top=min(k['y'] for k in after['keys'] if k['key']=='q')
                assert b[3]<key_top
                capture(a,OUT/f'docked-{count}.png')
                a.tap(b[0]+100,b[1]+34+46+22);t.wait_for(lambda:a.diag()['popup']=='')
                after=a.diag()['editor'];assert a.diag()['active_model']=='b'
                for name in ['session','preedit_length','ocr_request','build_count','keys']:assert after[name]==before[name],name
                assert not a.diag()['generating']
            finally:a.close()
        print('Portrait model menu passed:',count,flush=True)
finally:os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
