#!/usr/bin/env python3
"""ORANGE preset UI in an isolated database; restore user bytes afterwards."""
import json,os,pathlib,sqlite3,subprocess,sys,tempfile,time
import acceptance as d
from chat_acceptance import close_app,start,pull,push
from photo_orientation import move
sys.path.insert(0,str(d.ROOT/'tests'))
from test_reasoning_ui import seed
if __name__=='__main__':
    state=d.state()
    assert state['view']=='chat' and not state['keyboard'] and not state['generating']
    sdk=os.environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK')
    toolchain=os.environ.get('JSAPI_TOOLCHAIN_ROOT',sdk+'/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu')
    helper=d.ROOT/'.work/preset-tap'
    subprocess.run([toolchain+'/bin/aarch64-linux-gnu-g++','-std=c++11','-Os',str(d.ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
    push(helper,'/tmp/penchat-ui-tap');d.adb('chmod 755 /tmp/penchat-ui-tap')
    assert d.adb('test -x /tmp/penchat-ui-tap && echo ready')=='ready'
    host=d.adb('pidof miniapp')
    out=d.ROOT/'.work/device-presets';out.mkdir(parents=True,exist_ok=True)
    def session_count():
        pull(d.DATA+'/penchat.db',out/'count.db')
        with sqlite3.connect(out/'count.db') as db:return db.execute('select count(*) from chat_sessions').fetchone()[0]
    close_app();pull(d.DATA+'/penchat.db',out/'before.db')
    try:
        with tempfile.TemporaryDirectory() as folder:
            p=pathlib.Path(folder);seed(p,1)
            with sqlite3.connect(p/'penchat.db') as db:
                presets=[{'id':'default','name':'默认预设','model_id':'a','effort':'api_default'},
                         {'id':'expert','name':'专家模式','model_id':'b','effort':'max','model_when':'always','effort_when':'always','prompt_mode':'custom','prompt':'Device fixture only'}]
                for key,value in [('chat_presets',presets),('presets_enabled',True),('auto_title_enabled',False)]:
                    db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',(key,json.dumps(value)))
            push(p/'penchat.db',d.DATA+'/penchat.db')
        start();assert d.state()['effective_effort']=='api_default'
        d.tap(128,232);move(250,182,0,-72,0);time.sleep(.3);d.capture('presets-plus-scroll');d.tap(220,174);d.capture('presets-after-tap')
        assert d.state()['popup']=='presets'
        d.capture('presets-menu')
        d.tap(350,138)
        state=d.state();assert state['session_preferences']['model_id']=='b' and state['effective_effort']=='max'
        assert session_count()==0
        d.open_navigation();d.tap(180,236)
        assert d.state()['session_preferences']['model_id']=='a'
        d.open_settings();move(300,210,0,-110,0);time.sleep(.5);d.capture('presets-settings-scroll');d.tap(300,230)
        d.view('presets');d.capture('presets-settings')
        d.tap(300,76);d.tap(300,166);d.view('preset_edit');d.capture('presets-editor')
        d.tap(40,32);d.to_chat()
        d.tap(500,232);d.wait(lambda:d.state()['keyboard'])
        d.tap(35,35);d.wait(lambda:not d.state()['keyboard'])
        assert session_count()==0
    finally:
        close_app();push(out/'before.db',d.DATA+'/penchat.db');start()
        pull(d.DATA+'/penchat.db',out/'restored.db')
        assert (out/'before.db').read_bytes()==(out/'restored.db').read_bytes()
        assert d.adb('pidof miniapp')==host
        for name in ['before.db','restored.db','count.db']:(out/name).unlink(missing_ok=True)
    print('ORANGE presets: menu scrolling, apply, default new chat, settings/editor, IME and exact DB restoration passed.')
