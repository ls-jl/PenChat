#!/usr/bin/env python3
"""Actual LVGL settings layout, switch semantics and navigation restoration."""
import json, os, pathlib, signal, sqlite3, tempfile, time
from test_lvgl_app import Native, ROOT, wait_for
from test_reasoning_ui import seed
from settings_ui import locate

BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', ROOT/'build/host/penchat-lvgl'))
OUT = pathlib.Path(os.environ.get('PENCHAT_SETTINGS_ARTIFACTS', ROOT/'.work/settings-ui'))
OUT.mkdir(parents=True, exist_ok=True)
SWITCHES = ('image_auto_send', 'auto_title', 'auto_context', 'persistent_exports')
ROWS = ('models','presets','reasoning','image_auto_send','auto_title','title_model',
        'auto_context','archives','history_time','message_edit','font_size','keyboard','pull_sensitivity','compression_threshold','transfer','persistent_exports','http_idle','about')

def shot(app, name):
    path = app.directory/'lvgl-diagnostic.png'
    if path.exists(): path.unlink()
    app.process.send_signal(signal.SIGUSR1);wait_for(path.exists)
    (OUT/(name+'.png')).write_bytes(path.read_bytes())

def config(directory):
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
        return {k:json.loads(v) for k,v in db.execute('select key,value_json from app_settings')}

def show_top(app):
    locate(app.diag,app.settings_swipe,'models')
    # A newly entered Settings page is the authoritative first-screen position.
    app.to_chat();app.open_settings();assert app.diag()['scroll_y']==0

with tempfile.TemporaryDirectory(prefix='penchat-settings-') as folder:
    directory=pathlib.Path(folder)
    seed(directory,1)
    with sqlite3.connect(directory/'penchat.db') as db:
        db.execute("update model_configs set name=?,display_name=? where id='a'",
                   ('用于验证省略号的超长命名模型名称-'+('long-model-'*7),)*2)
        db.execute("insert into app_settings values('title_model_id','\"a\"',1)")
    app=Native(BINARY,directory)
    try:
        app.open_settings();state=app.diag();rows=state['settings']['rows']
        assert 'autoSendCapturedContent' not in config(directory).get('penchat_settings',{})
        assert set(rows)==set(ROWS)
        for name in ROWS:
            row=rows[name];x1,y1,x2,y2=row['bounds']
            assert y2-y1+1==56 and x2-x1+1>=44
            for key in ('title','detail'):
                a,b,c,d=row[key]
                assert x1<=a<=c<row['indicator'][0] and y1<=b<=d<=y2,(name,key,row)
            assert row['title'][3]<row['detail'][1],name
        assert rows['presets']['bounds'][0]-rows['models']['bounds'][2]-1==16
        assert all(48<=rows[k]['bounds'][1] and rows[k]['bounds'][3]<266 for k in ROWS[:6])
        assert state['settings']['viewport'][1:4:2]==[0,state['height']-1]
        assert state['settings']['heading'][1]==7
        shot(app,'first-screen-long-model')
        app.settings_swipe(-120)
        wait_for(lambda:not app.diag()['settings']['scrolling'])
        scrolled=app.diag()
        assert scrolled['settings']['heading'][3]<0
        assert scrolled['settings']['viewport'][1]==0
        # The old pinned-header strip is now part of the actionable viewport.
        upper=[k for k,v in scrolled['settings']['rows'].items() if 0<=v['bounds'][1]<48 and v['bounds'][3]<scrolled['height']]
        assert upper
        before=scrolled['scroll_y'];locate(app.diag,app.settings_swipe,upper[0])
        assert app.diag()['scroll_y']==before
        shot(app,'scrolled-heading-hidden')
        show_top(app)
        for desired in (True,False):
            for i,name in enumerate(SWITCHES):
                if app.diag()['settings']['rows'][name]['checked']!=desired:
                    app.setting(name,indicator=i%2==0)
                assert app.diag()['settings']['rows'][name]['checked']==desired
            show_top(app);shot(app,'switches-'+str(desired).lower())
            locate(app.diag,app.settings_swipe,'about');shot(app,'bottom-'+str(desired).lower())
        stored=config(directory)
        assert stored['penchat_settings']['autoSendCapturedContent'] is False
        assert all(stored[k] is False for k in ('auto_title_enabled','auto_context_compression','persistent_export_directory'))
        # An off automatic-title switch does not disable its model picker.
        row=locate(app.diag,app.settings_swipe,'title_model');before=app.diag()['scroll_y']
        app.setting('title_model');assert app.diag()['popup']=='title_models';shot(app,'title-model-picker')
        app.long_press(250,162);wait_for(lambda:app.diag()['modal']);shot(app,'full-model-name')
        assert config(directory)['title_model_id']=='a'
        app.tap(890,222);assert app.diag()['scroll_y']==before
        app.setting('title_model')
        app.tap(800,230);assert app.diag()['scroll_y']==before
        app.setting('title_model');app.tap(250,40)
        assert config(directory)['title_model_id']=='' and app.diag()['scroll_y']==before
        app.setting('reasoning');shot(app,'reasoning-picker');app.tap(800,96)
        assert config(directory)['reasoning_display']=='always'
        for name,view in [('archives','archives'),('presets','presets'),('transfer','transfer'),('models','provider')]:
            locate(app.diag,app.settings_swipe,name);before=app.diag()['scroll_y']
            app.setting(name);assert app.diag()['view']==view
            if view=='transfer':wait_for(lambda:not app.diag()['transfer_busy'])
            app.tap(40,32);assert app.diag()['view']=='settings' and app.diag()['scroll_y']==before,(name,app.diag()['scroll_y'],before)
        locate(app.diag,app.settings_swipe,'about');before=app.diag()['scroll_y']
        app.setting('about');assert app.diag()['modal'];shot(app,'about');app.tap(890,222)
        assert not app.diag()['modal'] and app.diag()['scroll_y']==before
        # A swipe beginning on a toggle row must scroll without activating it.
        show_top(app);row=app.diag()['settings']['rows']['image_auto_send']['bounds']
        x,y=(row[0]+row[2])//2,(row[1]+row[3])//2
        for dy in (0,-20,-40,-60):app.send('touch',x=x,y=y+dy,pressed=True);time.sleep(.04)
        app.send('touch',x=x,y=y-60,pressed=False);time.sleep(.2)
        assert app.diag()['settings']['rows']['image_auto_send']['checked'] is False
        show_top(app)
    finally:app.close()
    app=Native(BINARY,directory)
    try:
        app.open_settings()
        assert all(not app.diag()['settings']['rows'][name]['checked'] for name in SWITCHES)
    finally:app.close()
    with sqlite3.connect(directory/'penchat.db') as db:
        db.execute("update app_settings set value_json='\"deleted-model\"' where key='title_model_id'")
    app=Native(BINARY,directory)
    try:
        app.open_settings();shot(app,'unavailable-model')
        app.setting('title_model');app.tap(800,230)
        assert config(directory)['title_model_id']=='deleted-model'
    finally:app.close()

with tempfile.TemporaryDirectory(prefix='penchat-settings-empty-') as folder:
    app=Native(BINARY,folder,seed_model=False)
    try:
        app.open_settings();app.setting('title_model');shot(app,'empty-model-picker')
        app.tap(300,88);assert app.diag()['view']=='provider'
        app.tap(40,32);assert app.diag()['view']=='settings'
    finally:app.close()
print('Settings: '+str(len(ROWS))+' reachable controls, switch clicks/persistence, long values, pickers, swipe and child-return scroll passed.')
