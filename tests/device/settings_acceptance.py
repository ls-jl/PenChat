#!/usr/bin/env python3
"""ORANGE Settings acceptance, with private and public user files isolated."""
import json, sqlite3, subprocess, sys, time
import acceptance as d
from chat_acceptance import close_app, push
from native_editor import hashes
sys.path.insert(0,str(d.ROOT/'tests'))
from settings_ui import locate
from test_reasoning_ui import seed

SWITCHES=('image_auto_send','auto_title','auto_context','persistent_exports')
def capture(name):d.capture('settings-'+name)
def fixture():
    path=d.ROOT/'.work/device-settings';path.mkdir(exist_ok=True)
    db=path/'penchat.db'
    if db.exists():db.unlink()
    seed(path,1)
    with sqlite3.connect(db) as connection:
        assert connection.execute("select count(*) from app_settings where key='penchat_settings'").fetchone()[0]==0
        connection.execute("update model_configs set name=?,display_name=? where id='a'",('用于检查长名称省略的命名模型-'+('long-model-'*6),)*2)
        connection.execute("insert into app_settings values('title_model_id','\"a\"',1)")
    db.chmod(0o600)
    push(db,d.DATA+'/penchat.db')

def exercise():
    d.open_settings();assert d.state()['scroll_y']==0
    rows=d.state()['settings']['rows'];assert len(rows)==11
    for name in ('models','presets','reasoning','image_auto_send','auto_title','title_model'):
        a=rows[name]['bounds'];assert 48<=a[1] and a[3]<266
    capture('first-screen')
    assert d.state()['settings']['viewport'][1:4:2]==[0,d.state()['height']-1]
    d.settings_swipe(-120);d.wait(lambda:not d.state()['settings']['scrolling'])
    assert d.state()['settings']['heading'][3]<0
    capture('heading-scrolled-out')
    d.to_chat();d.open_settings()
    for desired in (False,True):
        for name in SWITCHES:
            if d.state()['settings']['rows'][name]['checked']!=desired:d.setting(name,indicator=True)
            current=d.state()
            print('Switch',name,desired,current['settings']['rows'][name]['checked'],'scroll',current['scroll_y'],flush=True)
            if current['settings']['rows'][name]['checked']!=desired:capture('switch-failure-'+name)
            assert current['settings']['rows'][name]['checked']==desired,(name,desired,current['settings']['rows'][name],current['settings']['scrolling'],current['touch'])
        d.to_chat();d.open_settings();capture('toggles-'+str(desired).lower())
        locate(d.state,d.settings_swipe,'about');capture('bottom-'+str(desired).lower())
    # Tap the row then the visual switch; each must change the setting once.
    d.setting('image_auto_send');assert not d.state()['settings']['rows']['image_auto_send']['checked']
    d.setting('image_auto_send',indicator=True);assert d.state()['settings']['rows']['image_auto_send']['checked']
    for name,view in [('archives','archives'),('presets','presets'),('transfer','transfer'),('models','provider')]:
        locate(d.state,d.settings_swipe,name);before=d.state()['scroll_y']
        d.setting(name);d.view(view)
        if view=='transfer':d.wait(lambda:not d.state()['transfer_busy'])
        d.tap(40,32);d.view('settings');assert d.state()['scroll_y']==before
    locate(d.state,d.settings_swipe,'title_model');before=d.state()['scroll_y']
    d.setting('title_model');d.wait(lambda:d.state()['popup']=='title_models');capture('model-picker')
    d.adb('/tmp/penchat-ui-tap --hold 275 709 650 2>/dev/null');d.wait(lambda:d.state()['modal']);capture('full-model-name')
    d.tap(890,222);assert d.state()['scroll_y']==before
    d.setting('title_model')
    d.tap(800,230);assert d.state()['scroll_y']==before
    d.setting('reasoning');d.wait(lambda:d.state()['modal']);capture('reasoning-picker');d.tap(890,222)
    locate(d.state,d.settings_swipe,'about');before=d.state()['scroll_y']
    d.setting('about');d.wait(lambda:d.state()['modal']);capture('about');d.tap(890,222)
    assert d.state()['scroll_y']==before
    d.to_chat();d.open_settings();assert d.state()['scroll_y']==0
    d.to_chat()

def main():
    assert 'ORANGE' in d.adb('cat /proc/device-tree/model')
    if d.pid():
        s=d.state()
        assert not s['generating'] and not s.get('compressing') and not s['title_busy'] and not s['keyboard'] and not s['editor']['active'] and not s['draft_length'] and not s['storage']['unsaved'],'Keep pending user work intact'
        close_app()
    # This helper supplies a settled swipe as well as ordinary real touches.
    sdk='/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK'
    compiler=sdk+'/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-g++'
    helper=d.ROOT/'.work/penchat-ui-tap'
    subprocess.run([compiler,'-std=c++11','-Os',str(d.ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
    push(helper,'/tmp/penchat-ui-tap');d.adb('chmod 755 /tmp/penchat-ui-tap')
    saved=d.DATA+'.settings-acceptance-saved'
    public='/userdisk/Favorite/PenChat';public_saved=public+'.settings-acceptance-saved'
    assert d.adb('test ! -e '+saved+' && test ! -e '+public_saved+' && echo ready')=='ready','Restore an earlier acceptance before testing'
    original=hashes(d.DATA);has_public=d.adb('test -d '+public+' && echo yes')=='yes'
    original_public=hashes(public) if has_public else [];host=d.adb('pidof miniapp')
    private_isolated=public_isolated=False
    try:
        d.adb('mv '+d.DATA+' '+saved+' && mkdir '+d.DATA);private_isolated=True
        if has_public:d.adb('mv '+public+' '+public_saved)
        public_isolated=True;fixture()
        d.adb('miniapp_cli start '+d.APP_ID);d.wait(d.pid);d.wait(lambda:d.state()['visible']);time.sleep(.6)
        exercise()
    finally:
        if d.pid():close_app()
        if private_isolated:d.adb('rm -rf '+d.DATA+' && mv '+saved+' '+d.DATA)
        if public_isolated:
            d.adb('rm -rf '+public)
            if has_public:d.adb('mv '+public_saved+' '+public)
        assert hashes(d.DATA)==original,'Private user files changed'
        if has_public:assert hashes(public)==original_public,'Public user files changed'
        assert d.adb('pidof miniapp')==host
    d.adb('miniapp_cli start '+d.APP_ID);d.wait(d.pid);d.wait(lambda:d.state()['visible'])
    print(json.dumps({'version':d.APP['version'],'controls':11,'switches':4,'child_scroll_restored':True,'private_files_restored':True,'public_files_restored':True,'host_unchanged':True}))

if __name__=='__main__':main()
