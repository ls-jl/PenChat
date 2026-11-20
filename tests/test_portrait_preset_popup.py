#!/usr/bin/env python3
"""Compact preset popup and retained draft/preedit; isolated DB, no API calls."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
OUT=t.ROOT/'.work/y07-preset-popup/screenshots';OUT.mkdir(parents=True,exist_ok=True)
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
t.port=1

def tap(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def open_presets(a):
    tap(a,a.diag()['attachment_button']);t.wait_for(lambda:a.diag()['popup']=='attachments')
    a.attachment_action('presets');t.wait_for(lambda:a.diag()['popup']=='presets');time.sleep(.2)
    return a.diag()['popup_bounds']

def key(a,char):
    k=next(k for k in a.diag()['editor']['keys'] if k['key']==char);a.tap(k['x'],k['y'])

os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
try:
    for count in [1,3,30]:
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
            a=t.Native(BINARY,p,seed_model=False,official_adapter=False,screen={'width':568,'height':1210,'direction':0})
            try:
                b=open_presets(a);d=a.diag();labels=d['presets_layout']['labels']
                assert len(labels)==count and all(x['role']=='title' for x in labels),labels
                row=max(44,max(x['line_height'] for x in labels)+8)
                assert b[3]-b[1]+1==min(1210-d['safe_insets'][1],8+count*row),b
                assert 0<=b[0]<b[2]<568 and not d['editor']['active']
                capture(a,OUT/f'closed-{count}.png')
                def reveal(index):
                    for _ in range(40):
                        b=a.diag()['popup_bounds'];row=a.diag()['presets_layout']['labels'][index]['row_bounds']
                        if b[1]<=row[1] and row[3]<=b[3]:return row
                        x=(b[0]+b[2])//2;up=row[3]>b[3]
                        y1=b[3]-35 if up else b[1]+35;y2=b[1]+35 if up else b[3]-35
                        for step in range(9):a.send('touch',x=x,y=y1+(y2-y1)*step//8,pressed=True);time.sleep(.025)
                        a.send('touch',x=x,y=y2,pressed=False);time.sleep(.2)
                    raise AssertionError('Preset row unreachable')
                if count>3:
                    row=reveal(-1);capture(a,OUT/'last-preset.png');tap(a,row)
                    t.wait_for(lambda:a.diag()['popup']=='')
                    assert a.diag()['session_preferences']['preset_id']=='p'+str(count-1)
                else:a.tap(540,500);assert not a.diag()['attachments']
                assert a.diag()['draft_length']==len('保留草稿')
                tap(a,a.diag()['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active'])
                key(a,'n');key(a,'i');t.wait_for(lambda:not a.diag()['editor']['pending'])
                before=a.diag()['editor'];b=open_presets(a);after=a.diag()['editor']
                assert 0<=b[0]<b[2]<568 and 0<=b[1]<b[3]<next(k['y'] for k in after['keys'] if k['key']=='q')
                for k in ['session','preedit_length','ocr_request','build_count','keys']:assert before[k]==after[k],k
                capture(a,OUT/f'docked-{count}.png')
                selected=a.diag()['presets_layout']['labels'][-1 if count>3 else 0]['row_bounds']
                assert b[1]<=selected[1]<selected[3]<=b[3],(b,selected)
                row=reveal(0)
                tap(a,row);t.wait_for(lambda:a.diag()['popup']=='')
                d=a.diag();assert d['session_preferences']['preset_id']=='default' and d['draft_length']==len('保留草稿')
                for k in ['session','preedit_length','ocr_request','build_count','keys']:assert before[k]==d['editor'][k],k
                assert not d['generating'] and d['summary']['sessions']==0
            finally:a.close()
        print('Portrait preset menu passed:',count,flush=True)
finally:os.environ.pop('PENCHAT_TEST_Y07_ROUNDED',None)
