#!/usr/bin/env python3
"""ORANGE section-page screenshots with the existing complete data isolation."""
import json,pathlib,sqlite3,sys,time
import settings_acceptance as isolated
import acceptance as d
from chat_acceptance import push
sys.path.insert(0,str(d.ROOT/'tests'))
from test_page_flow import png
original_fixture=isolated.fixture

def fixture():
    original_fixture()
    file=d.ROOT/'.work/device-settings/penchat.db'
    presets=[{'id':'default','name':'默认预设','prompt_mode':'inherit'}]+[
        {'id':f'flow-{i}','name':f'测试预设 {i}','prompt_mode':'inherit'} for i in range(12)]
    with sqlite3.connect(file) as db:
        db.execute("insert into app_settings values('chat_presets',?,1)",(json.dumps(presets),))
        for i in range(24):
            id=f'flow-session-{i}';status='archived' if i<12 else 'active'
            db.execute('insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,?,1,?,1)',(id,'测试会话 '+str(i),status,i+1))
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,?,1,'user','complete','text','fixture',1,1)",(id+'-message',id))
        for i in range(10):
            db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,created_at,updated_at) values(?,'p',?,?,'chat',1,1)",(f'flow-model-{i}',f'fixture-{i}',f'fixture-{i}'))
    push(file,d.DATA+'/penchat.db')
    folder=d.ROOT/'.work/page-flow-photos';folder.mkdir(exist_ok=True)
    d.adb('mkdir -p '+d.DATA+'/DCIM')
    for i in range(12):
        path=folder/f'fixture-{i}.png';png(path);push(path,d.DATA+'/DCIM/'+path.name)

def inspect(name):
    state=d.state();page=state['page'];assert page and page['viewport'][1:4:2]==[0,state['height']-1],name
    headingY=page['heading'][1]
    if name=='provider-list':assert page['actions']['添加 Provider'][1]==10
    d.capture('flow-'+name+'-top')
    maximum=state['scroll_y']+state['scroll_bottom']
    d.settings_swipe(-120);d.wait(lambda:not d.state()['page']['scrolling'])
    state=d.state();page=state['page'];assert page['heading'][1]==headingY-state['scroll_y']
    if maximum>=48:assert page['heading'][3]<0,(name,state)
    d.capture('flow-'+name+'-scrolled')
    for _ in range(30):
        if d.state()['scroll_y']<=0:
            d.wait(lambda:d.state()['scroll_y']==0);break
        d.settings_swipe(120);d.wait(lambda:not d.state()['page']['scrolling'])
    else:raise AssertionError('Title is not reachable')

def exercise():
    d.open_settings();inspect('settings')
    d.setting('models');inspect('provider-list')
    d.tap(870,120);d.view('model');inspect('model')
    d.page_action('参数映射');inspect('model-mappings');d.page_action('保存');d.view('provider')
    d.page_action('添加 Provider');inspect('provider-form')
    d.to_chat();d.open_settings();d.setting('presets');inspect('presets')
    d.tap(300,140);d.view('preset_edit');inspect('preset-editor');d.page_action('保存');d.view('presets')
    d.to_chat();d.open_settings();d.setting('archives');inspect('archives')
    d.to_chat();d.tap(128,232);d.tap(200,82);d.view('album');inspect('album')
    d.to_chat();d.open_settings();d.setting('transfer');d.wait(lambda:not d.state()['transfer_busy']);inspect('transfer')
    d.tap(250,105);d.wait(lambda:not d.state()['transfer_busy']);assert d.state()['transfer_screen']=='export_chats';inspect('transfer-export')
    d.to_chat();d.tap(128,232);d.tap(200,38);d.view('camera');d.wait(lambda:d.state()['camera_ready'])
    assert d.state()['page'] is None;d.capture('flow-camera')
    d.tap(40,32);d.view('chat')

if __name__=='__main__':
    isolated.fixture=fixture;isolated.exercise=exercise;isolated.main({'section_states':11,'camera_workspace':True,'private_files_restored':True,'public_files_restored':True,'host_unchanged':True})
