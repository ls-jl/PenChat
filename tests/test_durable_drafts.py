#!/usr/bin/env python3
"""Targeted durable draft and explicit effort checks with loopback-only credentials."""
import http.server,json,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_reasoning_ui import seed,gauge,select_effort
from image_gestures import capture,action
from test_photo_editor import begin

OUT=t.ROOT/'.work/durable-drafts';OUT.mkdir(parents=True,exist_ok=True)
class Server(t.Server):
    requests=[]
    fail=False
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append(body)
        if self.fail:
            self.send_response(500);self.send_header('Content-Length','0');self.end_headers();return
        wire=b'data: {"type":"response.output_text.delta","delta":"ok"}\n\ndata: {"type":"response.completed"}\n\n'
        self.send_response(200);self.send_header('Content-Length',str(len(wire)));self.end_headers();self.wfile.write(wire)

def tap(app,b):app.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def edit(app,value):
    tap(app,app.diag()['composer_text_bounds'])
    q=app.receive('keyboard.open')
    app.send('keyboard.result',requestId=q['requestId'],confirmed=True,text=value)
    t.wait_for(lambda:not app.diag()['keyboard'])
def new(app):
    app.open_navigation();tap(app,app.diag()['navigation_footer']['newchat']['bounds'])
    t.wait_for(lambda:not app.diag()['navigation_open'])

def run():
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);t.port=server.server_port
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix='penchat-draft-ui-') as d:
            p=pathlib.Path(d);seed(p,server.server_port)
            with sqlite3.connect(p/'penchat.db') as db:
                for key,value in [('auto_title_enabled',False),('new_chat_web','off'),('presets_enabled',True)]:
                    db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',(key,json.dumps(value)))
            app=t.Native(t.ROOT/'build/host/penchat-lvgl',p)
            try:
                edit(app,'给我解释')
                begin(app,p,w=80,h=60);action(app,'confirm')
                t.wait_for(lambda:app.diag()['view']=='chat' and not app.diag()['modal'])
                with sqlite3.connect(p/'penchat.db') as db:
                    assert db.execute('select count(*) from draft_attachments').fetchone()[0]==1
                new(app)
                assert app.diag()['draft_length']==len('给我解释')
                with sqlite3.connect(p/'penchat.db') as db:assert db.execute('select count(*) from chat_sessions').fetchone()[0]==0
            finally:app.close()
            app=t.Native(t.ROOT/'build/host/penchat-lvgl',p)
            try:
                assert app.diag()['draft_length']==len('给我解释')
                with sqlite3.connect(p/'penchat.db') as db:
                    stored=json.loads(db.execute('select image_json from draft_attachments').fetchone()[0])
                    assert pathlib.Path(stored['path']).is_file()
                app.attachment_action('effort');assert app.diag()['effort_gauge']
                assert app.diag()['effective_effort']=='medium'
                assert 'api_default' not in app.diag()['effort_choices']
                # Showing the neutral gauge keeps Provider default in the actual request.
                app.tap(908,232);t.wait_for(lambda:bool(Server.requests))
                t.wait_for(lambda:not app.diag()['generating'])
                assert Server.requests[-1]['reasoning']['effort']=='medium'
                assert 'data:image/' in json.dumps(Server.requests[-1])
                assert 'effort' not in app.diag()['session_preferences']
                gauge(app);select_effort(app,'high')
                assert app.diag()['session_preferences']['effort']=='high'
                app.attachment_action('effort')
                assert not app.diag()['effort_gauge'] and app.diag()['effective_effort']=='medium'
                new(app);assert app.diag()['draft_length']==0
                edit(app,'new draft')
                app.open_navigation();app.tap(240,36)
                t.wait_for(lambda:not app.diag()['navigation_open'])
                edit(app,'existing draft');new(app)
                assert app.diag()['draft_length']==len('new draft')
                app.attachment_action('presets')
                t.wait_for(lambda:app.diag()['popup']=='presets')
                assert all(x['role']=='title' for x in app.diag()['presets_layout']['labels'])
                time.sleep(.22);capture(app,OUT/'landscape-presets.png')
            finally:app.close()
            app=t.Native(t.ROOT/'build/host/penchat-lvgl',p)
            try:
                assert app.diag()['draft_length']==len('existing draft')
                before=len(Server.requests);app.tap(908,232)
                t.wait_for(lambda:len(Server.requests)>before)
                t.wait_for(lambda:not app.diag()['generating'])
                new(app);assert app.diag()['draft_length']==0
                Server.fail=True;edit(app,'failed response');app.tap(908,232)
                t.wait_for(lambda:not app.diag()['generating'])
                Server.fail=False
                begin(app,p,w=80,h=60);action(app,'confirm')
                t.wait_for(lambda:app.diag()['view']=='chat' and not app.diag()['modal'])
                before=len(Server.requests);app.tap(908,232)
                t.wait_for(lambda:len(Server.requests)>before)
                t.wait_for(lambda:not app.diag()['generating'])
                with sqlite3.connect(p/'penchat.db') as db:
                    assert db.execute("select count(*) from chat_messages where session_id=? and role='user' and message_type='image'",(app.diag()['session'],)).fetchone()[0]==1
            finally:Server.fail=False;app.close()
        # A Provider without a default emits no effort until the sole supported level is chosen.
        with tempfile.TemporaryDirectory(prefix='penchat-single-effort-') as d:
            p=pathlib.Path(d);seed(p,server.server_port)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute('update providers set metadata_json=?',(json.dumps({'api':'responses'}),))
                db.execute('update model_configs set capabilities_json=?',(json.dumps({'tool_support':'supported','reasoning':{'levels':{'low':'low'}}}),))
                db.execute("insert into app_settings values('auto_title_enabled','false',1)")
            app=t.Native(t.ROOT/'build/host/penchat-lvgl',p)
            try:
                app.attachment_action('effort');edit(app,'default omitted')
                before=len(Server.requests);app.tap(908,232)
                t.wait_for(lambda:len(Server.requests)>before and not app.diag()['generating'])
                assert 'effort' not in Server.requests[-1].get('reasoning',{})
                gauge(app);select_effort(app,'low')
                edit(app,'explicit low');before=len(Server.requests);app.tap(908,232)
                t.wait_for(lambda:len(Server.requests)>before and not app.diag()['generating'])
                assert Server.requests[-1]['reasoning']['effort']=='low'
            finally:app.close()
        # A large-font unsupported-search notice owns its action area above the composer.
        with tempfile.TemporaryDirectory(prefix='penchat-search-modal-') as d:
            p=pathlib.Path(d);seed(p,server.server_port)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("update model_configs set capabilities_json=?",(json.dumps({'tool_support':'unsupported','reasoning':{'levels':{'low':'low'}}}),))
                db.execute("insert into app_settings values('reading_font_percent','150',1)")
            app=t.Native(t.ROOT/'build/host/penchat-lvgl',p,ui_scale_percent=150)
            try:
                edit(app,'long draft '*50)
                app.tap(908,232)
                t.wait_for(lambda:app.diag()['modal'])
                capture(app,OUT/'search-modal-large-font.png')
                app.tap(850,200)
                t.wait_for(lambda:not app.diag()['modal'])
                assert app.diag()['draft_length']==len('long draft '*50)
            finally:app.close()
        # Portrait uses the same compact popup and preserves its retained editor.
        with tempfile.TemporaryDirectory(prefix='penchat-preset-ui-') as d:
            p=pathlib.Path(d);seed(p,server.server_port)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("insert into app_settings values('presets_enabled','true',1)")
                presets=[{'id':'default','name':'默认预设','prompt_mode':'custom','prompt':'saved'}]
                presets.extend({'id':f'p{i}','name':f'示例预设 {i}'+(' 很长的名称用于检查宽度和选中行可见' if i==9 else '')} for i in range(1,10))
                db.execute("insert into app_settings values('chat_presets',?,1)",(json.dumps(presets),))
            (p/'.updates').mkdir()
            identity=json.loads((t.ROOT/'package.json').read_text())
            (p/'.updates/restore.json').write_text(json.dumps({'appid':identity['appid'],'target':identity['version'],
                'snapshot':{'session':'preset-fixture','draft':'','preferences':{'model_id':'a','preset_id':'p9'},'scroll':0,'offset':0,'following':True}}))
            app=t.Native(t.ROOT/'build/host/penchat-lvgl',p,screen={'width':568,'height':1210,'direction':0,'xoffset':0,'yoffset':0})
            try:
                app.attachment_action('presets');t.wait_for(lambda:app.diag()['popup']=='presets')
                assert all(x['role']=='title' for x in app.diag()['presets_layout']['labels'])
                time.sleep(.22);capture(app,OUT/'portrait-presets.png')
                state=app.diag()['presets_layout'];last=state['labels'][-1]['row_bounds'];bounds=state['bounds']
                assert bounds[1]<=last[1] and last[3]<=bounds[3]
                app.tap(550,600);app.open_settings();app.setting('presets')
                capture(app,OUT/'preset-modified-settings.png')
            finally:app.close()
    finally:server.shutdown()
    print('Durable shared draft, restart, local send invalidation, neutral effort and compact presets passed.')
if __name__=='__main__':run()
