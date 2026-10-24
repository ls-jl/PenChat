#!/usr/bin/env python3
"""Exercise preset UI and actual HTTP bodies, with an isolated fake provider."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
from test_lvgl_app import Native,ROOT,wait_for
from test_reasoning_ui import seed,Server
from test_chat_style import drag,snapshot
import test_lvgl_app as transport
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
threading.Thread(target=server.serve_forever,daemon=True).start()
transport.port=server.server_port
Server.mode='none'
with tempfile.TemporaryDirectory(prefix='penchat-presets-') as directory:
    p=pathlib.Path(directory);seed(p,server.server_port)
    presets=[{'id':'default','name':'默认预设','prompt_mode':'custom','prompt':'DEFAULT SYSTEM','model_id':'a','effort':'api_default'},
             {'id':'expert','name':'专家','prompt_mode':'custom','prompt':'EXPERT SYSTEM','model_id':'b','effort':'max','model_when':'always','effort_when':'always'},
             {'id':'blank','name':'空白专用','prompt_mode':'inherit','model_id':'a','effort':'high','model_when':'empty','effort_when':'empty'}]
    with sqlite3.connect(p/'penchat.db') as db:
        for key,value in [('chat_presets',presets),('presets_enabled',True),('auto_title_enabled',False)]:
            db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',(key,json.dumps(value)))
    app=Native(binary,p)
    def picker():
        app.tap(128,232)
        drag(app,[190,150,110,70,20],x=250)
        snapshot(app,'preset-menu-scroll')
        app.tap(220,174)
        assert app.diag()['popup']=='presets',app.diag()['popup']
    def choose(row):
        picker();app.tap(350,66+row*72)
    def send(text):
        app.edit(text);app.tap(908,232);wait_for(lambda:not app.diag()['generating'])
    try:
        assert app.diag()['session_preferences']['system_prompt']=='DEFAULT SYSTEM'
        assert app.diag()['summary']['sessions']==0
        app.edit('保留草稿')
        choose(1)
        s=app.diag();assert s['session_preferences']['model_id']=='b' and s['effective_effort']=='max'
        assert s['summary']['sessions']==0
        app.tap(500,232);q=app.receive('keyboard.open');assert q['text']=='保留草稿'
        app.send('keyboard.result',requestId=q['requestId'],confirmed=False,text='')
        app.tap(908,232);wait_for(lambda:not app.diag()['generating'])
        body=Server.requests[-1][1]
        assert body['instructions']=='EXPERT SYSTEM' and body['reasoning']['effort']=='max' and body['model']=='b',body
        # Only-empty preset does not replace a sent conversation's model/effort.
        picker();drag(app,[175,130,90],x=350);app.tap(350,160)
        s=app.diag();assert s['session_preferences']['model_id']=='b',s['session_preferences']
        # Pull inherits the applied snapshot, including system instructions.
        app.to_chat();app.tap(521,174) if not app.diag()['following_latest'] else None
        time.sleep(.7);old=app.diag()['session']
        drag(app,[195,180,155,125,95,65,40])
        assert app.diag()['session']!=old
        assert app.diag()['session_preferences']['system_prompt']=='EXPERT SYSTEM'
        assert app.diag()['session_preferences']['model_id']=='b'
        # Drawer new chat applies the default, not the preceding preset.
        app.open_navigation();app.tap(180,236)
        s=app.diag();assert s['session_preferences']['model_id']=='a' and s['effective_effort']=='api_default'
        send('默认请求')
        body=Server.requests[-1][1];assert body['instructions']=='DEFAULT SYSTEM' and 'effort' not in body.get('reasoning',{})
        Server.mode='summary-delayed'
        app.edit('请求快照');app.tap(908,232)
        wait_for(lambda:app.diag()['generating'])
        choose(1)
        wait_for(lambda:not app.diag()['generating'])
        assert Server.requests[-1][1]['instructions']=='DEFAULT SYSTEM'
        assert app.diag()['session_preferences']['system_prompt']=='EXPERT SYSTEM'
        Server.mode='none';send('下一次使用专家配置')
        assert Server.requests[-1][1]['instructions']=='EXPERT SYSTEM'
        app.open_navigation();app.tap(180,236);send('恢复默认会话')
        picker();snapshot(app,'presets-picker');app.tap(900,232)
        app.open_settings();app.setting("presets")
        assert app.diag()['view']=='presets',app.diag()['view']
        snapshot(app,'presets-settings')
        app.tap(300,76) # hide entrance, preserve active preferences
        assert app.diag()['session_preferences']['system_prompt']=='DEFAULT SYSTEM'
        app.tap(300,166);assert app.diag()['view']=='preset_edit'
        snapshot(app,'presets-editor')
        app.tap(40,32);app.to_chat()
        app.close()
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("update providers set metadata_json=? where id='p'",(json.dumps({'api':'chat'}),))
        app=Native(binary,p)
        assert app.diag()['session_preferences']['system_prompt']=='DEFAULT SYSTEM'
        send('兼容接口')
        body=Server.requests[-1][1]
        assert body['messages'][0]=={'role':'system','content':'DEFAULT SYSTEM'}
        assert sum(m['role']=='system' for m in body['messages'])==1 and 'reasoning_effort' not in body
        app.open_settings();app.setting("presets")
        assert app.diag()['view']=='presets'
        app.page_action('新增预设');assert app.diag()['view']=='preset_edit'
        app.tap(300,76);q=app.receive('keyboard.open')
        app.send('keyboard.result',requestId=q['requestId'],confirmed=True,text='测试配置')
        wait_for(lambda:not app.diag()['keyboard'])
        app.page_action('保存');assert app.diag()['view']=='presets'
        with sqlite3.connect(p/'penchat.db') as db:
            saved=json.loads(db.execute("select value_json from app_settings where key='chat_presets'").fetchone()[0])
        created=saved[-1]['id'];assert saved[-1]['name']=='测试配置' and created!='default'
        drag(app,[220,170,100,70],x=300);time.sleep(.3);app.tap(300,230)
        assert app.diag()['view']=='preset_edit'
        drag(app,[220,170,100,70],x=700);time.sleep(.3);app.tap(700,230)
        assert app.diag()['modal']
        app.tap(200,225);assert app.diag()['view']=='presets'
        with sqlite3.connect(p/'penchat.db') as db:
            saved=json.loads(db.execute("select value_json from app_settings where key='chat_presets'").fetchone()[0])
        assert all(v['id']!=created for v in saved) and saved[0]['id']=='default'
        assert app.diag()['session_preferences']['system_prompt']=='DEFAULT SYSTEM'

    finally:app.close()
server.shutdown()
print('Presets: UI, draft retention, empty/sent rules, pull/drawer inheritance, restart and both HTTP protocols passed.')
