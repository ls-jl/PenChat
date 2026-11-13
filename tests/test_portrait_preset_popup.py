#!/usr/bin/env python3
"""Compact preset popup and retained draft/preedit; isolated DB, no API calls."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
OUT=t.ROOT/'.work/y07-preset-popup/screenshots';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def tap(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def open_presets(a):
    tap(a,a.diag()['attachment_button']);t.wait_for(lambda:a.diag()['popup']=='attachments')
    a.attachment_action('presets');t.wait_for(lambda:a.diag()['popup']=='presets')
    return a.diag()['popup_bounds']

def key(a,char):
    k=next(k for k in a.diag()['editor']['keys'] if k['key']==char);a.tap(k['x'],k['y'])

os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
try:
    for count in [1,3,10]:
        with tempfile.TemporaryDirectory(prefix='penchat-portrait-presets-') as folder:
            p=pathlib.Path(folder);seed(p,1)
            presets=[{'id':'default','name':'默认预设','prompt_mode':'inherit','model_id':'','effort':''}]
            for i in range(1,count):presets.append({'id':f'p{i}','name':'示例预设 '+str(i),'prompt_mode':'inherit','model_id':'','effort':''})
            if count==3:presets[-1]['name']='这是一个很长的预设名称，用于检查省略和菜单宽度'
            with sqlite3.connect(p/'penchat.db') as db:
                for k,v in [('chat_presets',presets),('presets_enabled',True)]:
                    db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',(k,json.dumps(v)))
            (p/'.updates').mkdir();app=json.loads((t.ROOT/'package.json').read_text())
            (p/'.updates/restore.json').write_text(json.dumps({'appid':app['appid'],'target':app['version'],'snapshot':{'session':'','draft':'保留草稿','preferences':{'model_id':'a','preset_id':'default','preset_name':'默认预设'},'scroll':0,'offset':0,'following':True}}))
            a=t.Native(t.ROOT/'build/host/penchat-lvgl',p,seed_model=False,official_adapter=False,screen={'width':568,'height':1210,'direction':0})
            try:
                b=open_presets(a);d=a.diag();assert b[3]-b[1]+1==min(360,36+count*72),b
                assert b[2]-b[0]+1<=440 and not d['editor']['active']
                capture(a,OUT/f'closed-{count}.png')
                if count>3:
                    x=(b[0]+b[2])//2
                    for _ in range(5):
                        for y in range(b[3]-30,b[1]+20,-30):a.send('touch',x=x,y=y,pressed=True);time.sleep(.02)
                        time.sleep(.15);a.send('touch',x=x,y=b[1]+30,pressed=False);time.sleep(.15)
                    capture(a,OUT/'last-preset.png');a.tap(b[0]+110,b[3]-50)
                    t.wait_for(lambda:a.diag()['popup']=='')
                    assert a.diag()['session_preferences']['preset_id']=='p9'
                else:a.tap(540,500);assert not a.diag()['attachments']
                assert a.diag()['draft_length']==len('保留草稿')
                tap(a,a.diag()['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active'])
                key(a,'n');key(a,'i');t.wait_for(lambda:not a.diag()['editor']['pending'])
                before=a.diag()['editor'];b=open_presets(a);after=a.diag()['editor']
                assert b[3]-b[1]+1<=360 and b[3]<next(k['y'] for k in after['keys'] if k['key']=='q')
                for k in ['session','preedit_length','ocr_request','build_count','keys']:assert before[k]==after[k],k
                capture(a,OUT/f'docked-{count}.png')
                a.tap(b[0]+100,b[1]+54);t.wait_for(lambda:a.diag()['popup']=='')
                d=a.diag();assert d['session_preferences']['preset_id']=='default' and d['draft_length']==len('保留草稿')
                for k in ['session','preedit_length','ocr_request','build_count','keys']:assert before[k]==d['editor'][k],k
                assert not d['generating'] and d['summary']['sessions']==0
            finally:a.close()
        print('Portrait preset menu passed:',count,flush=True)
finally:os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
