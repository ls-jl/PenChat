#!/usr/bin/env python3
"""Real UI, request bodies, session inheritance and opaque reasoning round trips."""
import http.server, json, os, pathlib, signal, sqlite3, tempfile, threading, time
from test_lvgl_app import Native, ROOT, wait_for, Server as BasicServer
from test_chat_style import drag
import test_lvgl_app as transport

class Server(BasicServer):
    requests=[]
    mode='summary'
    def log_message(self,*args):pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if '[penchat-title]' in json.dumps(body):
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
            payload={'choices':[{'delta':{'content':'推理会话'}}]} if self.path.endswith('/chat/completions') else {'type':'response.output_text.delta','delta':'推理会话'}
            self.wfile.write(('data: '+json.dumps(payload)+'\n\ndata: '+('[DONE]' if self.path.endswith('/chat/completions') else '{"type":"response.completed"}')+'\n\n').encode());self.wfile.flush();self.close_connection=True;return
        self.requests.append((self.path,body))
        mode=self.mode
        self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
        def emit(value):
            wire=('data: '+json.dumps(value,ensure_ascii=False)+'\n\n').encode()
            for byte in wire:self.wfile.write(bytes([byte]))
            self.wfile.flush()
        try:
            if self.path.endswith('/chat/completions'):
                emit({'choices':[{'delta':{'reasoning_content':'公开思考'}}]});time.sleep(.12)
                emit({'choices':[{'delta':{'content':'回答完成。'},'finish_reason':'stop'}]})
                self.wfile.write(b'data: [DONE]\n\n');self.wfile.flush()
            else:
                item={'id':'reason-'+str(len(self.requests)),'type':'reasoning','status':'completed','summary':[], 'encrypted_content':'opaque-test-only'}
                if mode!='none':
                    emit({'type':'response.output_item.added','output_index':0,'item':{'id':item['id'],'type':'reasoning'}})
                    if mode in ('summary','summary-delayed'):
                        emit({'type':'response.reasoning_summary_text.delta','output_index':0,'summary_index':0,'delta':'公开总结'})
                        item['summary']=[{'type':'summary_text','text':'公开总结'}]
                    time.sleep(1.8 if mode in ('delayed','summary-delayed') else .25)
                    emit({'type':'response.output_item.done','output_index':0,'item':item})
                emit({'type':'response.output_text.delta','output_index':0 if mode=='none' else 1,'delta':'回答完成。'})
                final=({'output':[item] if mode!='none' else []})
                final['output'].append({'type':'message','role':'assistant','content':[{'type':'output_text','text':'回答完成。'}]})
                emit({'type':'response.completed','response':final})
        except (BrokenPipeError,ConnectionResetError):pass
        self.close_connection=True

def seed(directory,port):
    schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
        db.executescript(schema)
        db.execute("insert into providers(id,name,type,base_url,api_key,metadata_json,created_at,updated_at) values('p','Fixture','openai-compatible',?,'test-only',?,1,1)",(f'http://127.0.0.1:{port}/v1',json.dumps({'api':'responses','default_effort':'medium'})))
        for id,levels in [('a',{'none':'none','medium':'medium','high':'high','xhigh':'xhigh'}),('b',{'high':'high','max':'max'})]:
            db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,capabilities_json,is_default,created_at,updated_at) values(?,'p',?,?,'chat',?,?,1,1)",(id,id,id,json.dumps({'reasoning':{'levels':levels,'summary':True}}),int(id=='a')))


def screenshot(app,name):
    app.process.send_signal(signal.SIGUSR1);time.sleep(.2)
    out=ROOT/'.work/reasoning-ui';out.mkdir(parents=True,exist_ok=True)
    (out/(name+'.png')).write_bytes((app.directory/'lvgl-diagnostic.png').read_bytes())

def pick_model(app,second=False):
    app.tap(128,232);app.tap(220,126)
    assert app.diag()['popup']=='models'
    screenshot(app,'models')
    app.tap(300,114 if second else 68)

def gauge(app):
    if not app.diag()['effort_gauge']:
        app.tap(128,232);app.tap(220,170)
    app.tap(858,232);assert app.diag()['popup']=='effort'

def send(app,text):
    app.edit(text);app.tap(908,232)
    wait_for(lambda:not app.diag()['generating'])
    assert not app.diag()['modal']

def run():
    binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server, tempfile.TemporaryDirectory(prefix='penchat-reasoning-') as directory:
        transport.port=server.server_port;seed(directory,server.server_port)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        app=Native(binary,pathlib.Path(directory))
        try:
            first=app.diag()['session']
            drag(app,[190,170,140,110,80,50,38]);assert app.diag()['session']==first and not app.diag()['pull_active']
            app.edit('未发送草稿');drag(app,[190,170,140,110,80,50,38]);assert app.diag()['session']==first
            app.open_navigation()
            for points in [[(800,100),(800,50),(800,100)],[(800,100),(900,100),(800,100)]]:
                for x,y in points:app.send('touch',x=x,y=y,pressed=True);time.sleep(.03)
                app.send('touch',x=800,y=100,pressed=False);time.sleep(.05)
                assert app.diag()['navigation_open']
            app.send('touch',x=800,y=100,pressed=True);app.send('touch',x=800,y=100,pressed=False,cancelled=True)
            assert app.diag()['navigation_open']
            app.tap(800,100);assert not app.diag()['navigation_open']
            pick_model(app,True)
            assert app.diag()['active_model']=='b' and app.diag()['summary']['sessions']==0
            assert app.diag()['effective_effort']=='' and app.diag()['supported_efforts']==['high','max']
            gauge(app);screenshot(app,'two-levels');app.tap(896,110)
            assert app.diag()['effective_effort']=='max'
            app.tap(800,230);assert not app.diag()['attachments']
            assert app.diag()['send_center_x2']==app.diag()['capsule_endcap_x2']
            send(app,'首次提问')
            body=Server.requests[-1][1]
            assert body['model']=='b' and body['reasoning']['effort']=='max' and body['reasoning']['summary']=='auto'
            assert app.diag()['summary']['sessions']==1 and app.diag()['reasoning_header_count']==1
            assert 'opaque-test-only' not in json.dumps(app.diag())
            d=app.diag();app.tap(d['reasoning_header_x'],d['reasoning_header_y'])
            assert app.diag()['reasoning_expanded_count']==1;screenshot(app,'expanded-summary')
            with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
                reason,meta=db.execute("select reasoning_text,metadata_json from chat_messages where role='assistant'").fetchone()
                assert reason=='公开总结'
                meta=json.loads(meta);assert meta['reasoning_observed'] and meta['reasoning_duration_ms']>=100
            app.tap(521,174);wait_for(lambda:app.diag()['toolbar_y']==208)
            send(app,'同模型继续')
            assert any(item.get('encrypted_content')=='opaque-test-only' for item in Server.requests[-1][1]['input'])
            gauge(app);app.tap(840,38)
            assert app.diag()['effective_effort']==''
            send(app,'服务端默认')
            assert 'effort' not in Server.requests[-1][1].get('reasoning',{})
            gauge(app);app.tap(896,110);app.tap(800,230)
            Server.mode='delayed';app.edit('生成中切换模型');app.tap(908,232)
            wait_for(lambda:app.diag()['generating'])
            pick_model(app)
            assert app.diag()['generating']
            wait_for(lambda:not app.diag()['generating'])
            assert Server.requests[-1][1]['model']=='b' and Server.requests[-1][1]['reasoning']['effort']=='max'
            Server.mode='summary'
            assert app.diag()['active_model']=='a' and app.diag()['effective_effort']=='medium'
            send(app,'切换模型继续')
            assert not any(item.get('encrypted_content') for item in Server.requests[-1][1]['input'])
            assert Server.requests[-1][1]['reasoning']['effort']=='medium'
            gauge(app);screenshot(app,'four-levels');app.tap(708,110);app.tap(800,230)
            assert app.diag()['effective_effort']=='high'
            app.open_navigation();app.tap(180,236)
            assert app.diag()['active_model']=='a' and app.diag()['effective_effort']=='high'
            send(app,'新聊天沿用本次强度')
            assert Server.requests[-1][1]['reasoning']['effort']=='high'
            app.open_settings();app.setting("models");app.tap(870,175)
            assert app.diag()['view']=='model';screenshot(app,'model-capabilities')
            app.tap(700,200)  # Disable max: only High remains on model b.
            app.tap(700,24);screenshot(app,'model-mapping')
            app.tap(400,104);request=app.receive('keyboard.open')
            app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='custom-high')
            wait_for(lambda:not app.diag()['keyboard'])
            app.tap(860,24);app.to_chat()
        finally:app.close()
        app=Native(binary,pathlib.Path(directory))
        try:
            assert app.diag()['effective_effort']=='high' and app.diag()['effort_gauge']
            app.open_navigation();app.tap(180,236)
            assert app.diag()['effective_effort']=='medium' and app.diag()['active_model']=='a'
            Server.mode='encrypted';send(app,'仅加密思考')
            assert app.diag()['reasoning_header_count']==1
            d=app.diag();app.tap(d['reasoning_header_x'],d['reasoning_header_y']);assert app.diag()['reasoning_expanded_count']==0
            screenshot(app,'encrypted-time')
            app.open_navigation();app.tap(180,236)
            Server.mode='none';send(app,'普通回答')
            assert app.diag()['reasoning_header_count']==0;screenshot(app,'no-reasoning')
        finally:app.close()
        # Switch only the seeded fake provider to the existing Chat Completions path.
        with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
            db.execute("update providers set metadata_json=? where id='p'",(json.dumps({'api':'chat','default_effort':'high'}),))
        app=Native(binary,pathlib.Path(directory))
        try:
            app.open_navigation();app.tap(180,236);pick_model(app,True)
            assert app.diag()['supported_efforts']==['high']
            gauge(app);screenshot(app,'one-level');app.tap(896,110);app.tap(800,230)
            send(app,'兼容接口')
            body=Server.requests[-1][1]
            assert body['reasoning_effort']=='custom-high' and 'reasoning' not in body
            assert app.diag()['reasoning_header_count']==1
        finally:app.close()
        with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
            db.execute("update model_configs set enabled=0 where id='b'")
        app=Native(binary,pathlib.Path(directory))
        try:
            assert app.diag()['active_model']==''
            count=len(app.read_messages());app.edit('模型停用后仍保留草稿');app.tap(908,232)
            assert app.diag()['modal'] and len(app.read_messages())==count
            app.tap(889,220);app.tap(500,232);request=app.receive('keyboard.open')
            assert request['text']=='模型停用后仍保留草稿'
            app.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='')
        finally:app.close()
        server.shutdown()
    print('Reasoning UI: empty pulls, backdrop tap-only, model/session defaults, effort hierarchy, mappings, summaries, encrypted replay, timing and Chat Completions passed.')
if __name__=='__main__':run()
