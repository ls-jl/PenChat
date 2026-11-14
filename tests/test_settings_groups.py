#!/usr/bin/env python3
"""The same 25 settings form four measured sections in all layout classes."""
import json,pathlib,sqlite3,tempfile,time
import test_lvgl_app as ui
from test_reasoning_ui import seed
from settings_ui import locate
from provider_ui import capture
from choice_ui import select,dismiss
ui.port=1
OUT=ui.ROOT/'.work/settings-groups';OUT.mkdir(parents=True,exist_ok=True)
GROUPS=[('models',['models']),('tools',['web_policy']),
 ('chat',['presets','reasoning','auto_title','title_model','auto_context','compression_threshold',
          'image_auto_send','history_time','message_edit','retry_archive_prompt','archives','pull_sensitivity','composer_shortcut']),
 ('app',['theme','ui_scale','font_size','keyboard','transfer','persistent_exports','http_idle','update','feedback','about'])]
FULL={'models','web_policy','composer_shortcut','feedback','about'}

def geometry(a,w,h):
    s=a.diag()['settings'];rows=s['rows'];groups=s['groups']
    assert set(rows)=={k for _,items in GROUPS for k in items} and len(rows)==25
    assert [g['id'] for g in groups]==[g for g,_ in GROUPS]
    full_width=w-(32 if h>w else 118);wide=w>=3*h
    previous=None
    for group,(_,items) in zip(groups,GROUPS):
        head=group['bounds']
        if previous is not None:assert head[1]-previous-1==16,(group,head,previous)
        ordered=sorted(items,key=lambda k:(rows[k]['bounds'][1],rows[k]['bounds'][0]))
        assert ordered==items,ordered
        assert rows[items[0]]['bounds'][1]-head[3]-1==8
        for name in items:
            r=rows[name];b=r['bounds'];expected=full_width if not wide or name in FULL else (full_width-16)//2
            assert b[2]-b[0]+1==expected and b[3]-b[1]+1==56,(name,r)
            assert r['title'][3]<r['detail'][1] and r['detail'][3]<=b[3],(name,r)
            assert r['title'][2]<r['indicator'][0] and r['detail'][2]<r['indicator'][0]
        previous=max(rows[k]['bounds'][3] for k in items)
        if not wide:
            for left,right in zip(items,items[1:]):assert rows[right]['bounds'][1]-rows[left]['bounds'][3]-1==8
    if wide:
        for left,right in [('presets','reasoning'),('auto_title','title_model'),('auto_context','compression_threshold'),
          ('image_auto_send','history_time'),('message_edit','retry_archive_prompt'),('archives','pull_sensitivity'),
          ('theme','ui_scale'),('font_size','keyboard'),('transfer','persistent_exports'),('http_idle','update')]:
            l=rows[left]['bounds'];r=rows[right]['bounds'];assert l[1]==r[1] and r[0]-l[2]-1==16,(left,right)
    return s

for w,h in [(960,266),(640,260),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-settings-groups-') as folder:
        p=pathlib.Path(folder);seed(p,1)
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("update model_configs set display_name=? where id='a'",('很长的命名模型名称用于边界验证-'+('Model-'*15),))
            db.execute("insert into app_settings values('title_model_id','\"a\"',1)")
        a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',p,screen={'width':w,'height':h,'direction':0})
        try:
            a.open_settings();geometry(a,w,h);capture(a,OUT,f'{w}-top')
            # Verify every entry can be fully reached without invoking paid models or update checks.
            for group,items in GROUPS:
                for name in items:locate(a.diag,a.settings_swipe,name)
                locate(a.diag,a.settings_swipe,items[0]);capture(a,OUT,f'{w}-{group}')
            locate(a.diag,a.settings_swipe,'about');capture(a,OUT,f'{w}-bottom')
            locate(a.diag,a.settings_swipe,'retry_archive_prompt');before=a.diag()['scroll_y']
            a.setting('retry_archive_prompt');assert a.diag()['scroll_y']==before
            assert a.diag()['settings']['rows']['retry_archive_prompt']['checked']
            a.setting('reasoning');before=a.diag()['scroll_y'];select(a,'during')
            assert a.diag()['scroll_y']==before
            a.setting('history_time');before=a.diag()['scroll_y'];dismiss(a)
            assert a.diag()['scroll_y']==before
            # Children return to the same position and category in the one Settings reader.
            for name,view in [('presets','presets'),('archives','archives'),('keyboard','keyboard_settings'),('transfer','transfer'),('models','provider')]:
                locate(a.diag,a.settings_swipe,name);before=a.diag()['scroll_y'];a.setting(name)
                assert a.diag()['view']==view
                if view=='transfer':ui.wait_for(lambda:not a.diag()['transfer_busy'])
                a.tap(48,48) if h>w else a.tap(40,32)
                assert a.diag()['view']=='settings' and a.diag()['scroll_y']==before,(name,a.diag()['scroll_y'],before)
            geometry(a,w,h)
        finally:a.close()
        a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',p,screen={'width':w,'height':h,'direction':0})
        try:
            a.open_settings();geometry(a,w,h)
            assert a.diag()['scroll_y']==0 and a.diag()['settings']['rows']['retry_archive_prompt']['checked']
            with sqlite3.connect(p/'penchat.db') as db:
                assert json.loads(db.execute("select value_json from app_settings where key='reasoning_display'").fetchone()[0])=='during'
        finally:a.close()
        print('Four settings groups passed:',w,h,flush=True)
print('FOUR_GROUPS_25_CONTROLS_GEOMETRY_REACHABILITY_SCROLL_AND_PERSISTENCE_OK')
