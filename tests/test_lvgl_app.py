#!/usr/bin/env python3
"""Exercise the actual native executable over its inherited launcher transport."""
import contextlib, http.server, json, os, pathlib, signal, socket, sqlite3, subprocess, tempfile, threading, time
ROOT = pathlib.Path(__file__).resolve().parents[1]
REPLY = '# 中文回答\n\n**重要**：$\\frac{1}{2}$\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n```cpp\nint answer = 42;\n```\n'
class Server(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    requests = []
    def log_message(self, *args): pass
    def do_GET(self):
        body = json.dumps({'data': [{'id': 'fixture'}]}).encode()
        self.send_response(200); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if '[penchat-title]' in json.dumps(body):
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
            payload={'choices':[{'delta':{'content':'ORANGE 会话'}}]} if self.path.endswith('/chat/completions') else {'type':'response.output_text.delta','delta':'ORANGE 会话'}
            self.wfile.write(('data: '+json.dumps(payload)+'\n\ndata: '+('[DONE]' if self.path.endswith('/chat/completions') else '{"type":"response.completed"}')+'\n\n').encode());self.wfile.flush();self.close_connection=True;return
        self.requests.append((self.path, body))
        model = body['model']
        if model == 'error':
            payload = b'{"error":{"message":"fixture unauthorized"}}'
            self.send_response(401); self.send_header('Content-Length', str(len(payload))); self.end_headers(); self.wfile.write(payload); return
        self.send_response(200); self.send_header('Content-Type', 'text/event-stream'); self.send_header('Connection', 'close'); self.end_headers()
        try:
            pieces = ['\n\n'.join('第 %d 段：滚动保持测试。' % i for i in range(30)), '\n\n继续回复。', '\n\n回复完成。'] if model == 'scroll' else list(REPLY) if model == 'cancel' else [REPLY[:13], REPLY[13:35], REPLY[35:]]
            for piece in pieces:
                payload = {'choices': [{'delta': {'content': piece}}]} if self.path.endswith('/chat/completions') else {'type': 'response.output_text.delta', 'delta': piece}
                wire = ('data: ' + json.dumps(payload, ensure_ascii=False) + '\r\n\r\n').encode()
                # Boundaries deliberately split multibyte UTF-8, CRLF, and JSON tokens.
                for i in range(0, len(wire), 7): self.wfile.write(wire[i:i+7]); self.wfile.flush()
                time.sleep(1.5 if model == 'scroll' else .12 if model == 'cancel' else .04)
            if model != 'cut':
                final = '[DONE]' if self.path.endswith('/chat/completions') else json.dumps({'type': 'response.completed'})
                self.wfile.write(('data: ' + final + '\n\n').encode()); self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError): pass
        self.close_connection = True

def wait_for(callback, timeout=10):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        result = callback()
        if result: return result
        time.sleep(.03)
    raise AssertionError('Timed out waiting for native state')

class Native:
    def __init__(self, binary, directory, model='fixture', api='responses', seed_model=True, official_adapter=True):
        self.pending=[]
        self.official_adapter=official_adapter
        self.directory=pathlib.Path(directory);self.directory.mkdir(parents=True,exist_ok=True)
        dbpath=self.directory/'penchat.db'
        if not dbpath.exists():
            schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
            with sqlite3.connect(dbpath) as db:
                db.executescript(schema)
                if seed_model:
                    db.execute("insert into providers(id,name,type,base_url,api_key,metadata_json,created_at,updated_at,is_default) values('fixture-provider','Fixture','openai-compatible',?,'test-only-no-real-key',?,1,1,1)",(f'http://127.0.0.1:{port}/v1',json.dumps({'api':api})))
                    db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,created_at,updated_at,is_default) values('fixture-model','fixture-provider',?,?,'chat',1,1,1)",(model,model))
        self.parent, child=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        self.parent.settimeout(float(os.environ.get("PENCHAT_TEST_TIMEOUT", "10")))
        self.log=(self.directory/'runtime.log').open('wb')
        self.process=subprocess.Popen([str(binary),'--headless','--bridge-fd',str(child.fileno()),'--data-dir',str(self.directory),'--workspace',str(ROOT),'--font',str(ROOT/'.deps/chinese.ttf'),'--tex-resources',str(ROOT/'.deps/microtex/res')],pass_fds=[child.fileno()],stdout=self.log,stderr=self.log)
        child.close();self.generation=17
        self.send('init')
        try:self.receive('ready')
        except BaseException:
            self.parent.close()
            if self.process.poll() is None:self.process.kill()
            self.process.wait();self.log.close()
            raise RuntimeError("Native startup failed: "+(self.directory/'runtime.log').read_text(errors='replace')[-12000:])
    def send(self, kind, **kwargs):
        self.parent.send(json.dumps({'v':1,'generation':self.generation,'type':kind,**kwargs}).encode())
        # Existing business tests use the visible official fallback. Dedicated
        # editor tests disable this adapter and exercise native composition.
        if self.official_adapter and kind=='keyboard.result' and not kwargs.get('confirmed',False):
            time.sleep(.12);self.tap(630,236)

    def receive(self, kind):
        if kind=='keyboard.open' and self.official_adapter:
            wait_for(lambda:self.diag()['editor']['active']);self.tap(820,236)
        for i,value in enumerate(self.pending):
            if value.get("type")==kind:return self.pending.pop(i)
        while True:
            value=json.loads(self.parent.recv(65536))
            if value.get('type')==kind:return value
            assert value.get('type')!='fatal',value
            self.pending.append(value)
    def tap(self,x,y):
        self.send('touch',x=x,y=y,pressed=True);time.sleep(.06);self.send('touch',x=x,y=y,pressed=False);time.sleep(.08)
    def long_press(self,x,y):
        self.send('touch',x=x,y=y,pressed=True);time.sleep(.65);self.send('touch',x=x,y=y,pressed=False);time.sleep(.08)
    def diag(self):
        self.send('diagnostic');return self.receive('diagnostic')
    def to_chat(self):
        for _ in range(8):
            state=self.diag()
            if state['navigation_open']:
                self.tap(800,80)
            elif state['view']=='chat':return
            else:self.tap(40,32)
        raise AssertionError('Cannot return to chat')
    def open_navigation(self):
        self.to_chat();self.tap(40,32)
        assert self.diag()['navigation_open']
    def open_settings(self):
        self.open_navigation();self.tap(500,236)
        assert self.diag()['view']=='settings'
    def settings_swipe(self, delta):
        start=220 if delta<0 else 75
        for i in range(7):
            self.send('touch',x=500,y=start+int(delta*i/6),pressed=True);time.sleep(.025)
        time.sleep(.2) # Settle at the destination before release, without a fling.
        self.send('touch',x=500,y=start+delta,pressed=False)
    def setting(self, name, indicator=False):
        from settings_ui import activate
        return activate(self.diag,self.tap,self.settings_swipe,name,indicator)
    def read_messages(self):
        with sqlite3.connect(self.directory/'penchat.db') as db:
            return db.execute('select role,status,output_text,error_json from chat_messages order by created_at,seq').fetchall()
    def edit(self,value):
        wait_for(lambda:self.diag()['toolbar_y']==208)
        self.tap(500,232);request=self.receive('keyboard.open')
        self.send('keyboard.result',requestId='stale',confirmed=True,text='must not appear')
        assert self.diag()['keyboard'] is True
        self.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=value)
        wait_for(lambda:not self.diag()['keyboard'])
    def close(self):
        self.parent.close()
        assert self.process.wait(timeout=8)==0,(self.process.returncode,self.directory,(self.directory/'runtime.log').read_text(errors='replace')[-12000:])
        self.log.close()

if __name__ == '__main__':
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server:
        port=server.server_port
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        artifacts=ROOT/'.work/host-acceptance';artifacts.mkdir(parents=True,exist_ok=True)
        binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',str(ROOT/'build/host/penchat-lvgl')))
        results=[]
        for model,api in [('fixture','responses'),('fixture','chat'),('error','responses'),('cut','responses'),('cancel','responses')]:
            directory=pathlib.Path(tempfile.mkdtemp(prefix='penchat-ui-'))
            app=Native(binary,directory,model,api)
            try:
                assert app.diag()['view']=='chat'
                app.edit('你好，ORANGE')
                app.tap(908,232)
                if model=='cancel':
                    wait_for(lambda:any(r[0]=='assistant' and r[2] for r in app.read_messages()))
                    app.tap(908,232)
                wait_for(lambda:any(r[0]=='assistant' and r[1] in ['complete','error','cancelled'] for r in app.read_messages()))
                messages=app.read_messages();assistant=[r for r in messages if r[0]=='assistant'][-1]
                expected='cancelled' if model=='cancel' else 'error' if model in ['error','cut'] else 'complete'
                assert assistant[1]==expected,(model,assistant)
                if expected=='complete':
                    assert assistant[2]==REPLY,assistant
                    wait_for(lambda:not app.diag()['generating'])
                    time.sleep(.2)
                    before_scroll=app.diag()['scroll_y']
                    app.edit('保留草稿和滚动位置')
                    assert app.diag()['scroll_y']==before_scroll
                    app.tap(500,232);request=app.receive('keyboard.open')
                    app.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='discard')
                    wait_for(lambda:not app.diag()['keyboard'])
                    assert app.diag()['scroll_y']==before_scroll
                assert messages[0][2]=='你好，ORANGE'
                if model=='error':
                    assert '401' in assistant[3]
                    wait_for(lambda:app.diag()['modal'])
                    session=app.diag()['session']
                    for y in [190,165,140,115,90]:
                        app.send('touch',x=500,y=y,pressed=True);time.sleep(.025)
                    app.send('touch',x=500,y=90,pressed=False)
                    assert not app.diag()['pull_active'] and app.diag()['session']==session
                if api=='chat':assert Server.requests[-1][0].endswith('/chat/completions') and 'messages' in Server.requests[-1][1]
                app.process.send_signal(signal.SIGUSR1)
                wait_for(lambda:(directory/'lvgl-diagnostic.png').exists())
                (artifacts/f'{model}-{api}.png').write_bytes((directory/'lvgl-diagnostic.png').read_bytes())
                if model=='fixture' and api=='responses':
                    app.open_settings();app.setting("models");app.tap(790,30);app.tap(250,120)
                    request=app.receive('keyboard.open')
                    assert request['inputType']=='EnUSOnly' and request['maxlength']==4096
                    dummy_key='sk-'+'0123456789abcdef'*2
                    app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=dummy_key)
                    wait_for(lambda:not app.diag()['keyboard'])
                    app.tap(250,120);request=app.receive('keyboard.open')
                    assert request['text']==dummy_key
                    app.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='')
                    wait_for(lambda:not app.diag()['keyboard'])
                    app.tap(40,32)  # discard the unsaved new Provider
                    with sqlite3.connect(directory/'penchat.db') as db:
                        original=db.execute('select id,api_key from providers').fetchone()
                        original_model=db.execute('select id from model_configs where is_default=1').fetchone()[0]
                    app.tap(870,70);app.tap(250,120);request=app.receive('keyboard.open')
                    assert request['text']==original[1]
                    app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=dummy_key)
                    wait_for(lambda:not app.diag()['keyboard']);app.tap(870,24)
                    with sqlite3.connect(directory/'penchat.db') as db:
                        assert db.execute('select id,api_key from providers').fetchall()==[(original[0],dummy_key)]
                        assert db.execute('select id from model_configs where is_default=1').fetchone()[0]==original_model
                    # Key OCR is part of the common native editor and still requires Provider Save.
                    app.official_adapter=False
                    app.tap(870,70);app.tap(250,120)
                    scan=app.receive('ocr.start')
                    while scan['requestId']!=app.diag()['editor']['ocr_request']:scan=app.receive('ocr.start')
                    app.send('ocr.result',requestId='stale',session=scan['session'],mode='single',capture=1,confirmed=True,text='must-not-arrive')
                    before=app.diag()['editor']['length']
                    app.send('ocr.result',requestId=scan['requestId'],session=scan['session'],mode='single',capture=1,confirmed=True,text='-ocr-fixture')
                    wait_for(lambda:app.diag()['editor']['length']==before+12)
                    with sqlite3.connect(directory/'penchat.db') as db:assert db.execute('select api_key from providers').fetchone()[0]==dummy_key
                    app.tap(880,236)
                    with sqlite3.connect(directory/'penchat.db') as db:assert db.execute('select api_key from providers').fetchone()[0]==dummy_key
                    app.tap(870,24)
                    with sqlite3.connect(directory/'penchat.db') as db:assert db.execute('select id,api_key from providers').fetchall()==[(original[0],dummy_key+'-ocr-fixture')]
                    app.tap(870,70);app.tap(250,120)
                    scan=app.receive('ocr.start')
                    while scan['requestId']!=app.diag()['editor']['ocr_request']:scan=app.receive('ocr.start')
                    app.tap(630,236)
                    app.send('ocr.result',requestId=scan['requestId'],session=scan['session'],mode='single',capture=2,confirmed=True,text='late')
                    assert not app.diag()['editor']['active'] and not app.diag()['ocr']
                results.append({'model':model,'api':api,'status':assistant[1]})
            finally:app.close()
        with tempfile.TemporaryDirectory(prefix='penchat-scroll-') as directory:
            app=Native(binary,pathlib.Path(directory),'scroll')
            try:
                assert app.diag()['composer_width']==726
                app.edit('滚动测试');app.tap(908,232)
                wait_for(lambda:app.diag()['scroll_y']>400)
                app.send('touch',x=500,y=55,pressed=True)
                for y in range(65,231,15):
                    app.send('touch',x=500,y=y,pressed=True);time.sleep(.025)
                app.send('touch',x=500,y=230,pressed=False);time.sleep(.8)
                scrolled=app.diag()
                assert not scrolled['following_latest'] and scrolled['to_bottom_visible']
                assert scrolled['toolbar_y']>=266
                assert scrolled['to_bottom_x']==499 and scrolled['to_bottom_y']==152
                assert scrolled['to_bottom_opacity']==160
                app.open_navigation();assert app.diag()['navigation_open']
                wait_for(lambda:not app.diag()['generating'])
                assert app.diag()['navigation_open']
                app.tap(700,80)
                finished=app.diag()
                assert finished['scroll_y']==scrolled['scroll_y'],(scrolled,finished)
                assert finished['to_bottom_visible'] and not finished['following_latest']
                app.tap(521,174);time.sleep(.2)
                returned=app.diag()
                assert returned['following_latest'] and returned['scroll_bottom']==0
                assert not returned['to_bottom_visible'] and returned['toolbar_y']==208
            finally:app.close()
        server.shutdown()
        (artifacts/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print('Actual LVGL process: Chinese edit bridge, stale results, Responses, Chat Completions, SSE fragmentation, HTTP 401, truncated stream, cancellation, screenshots, socket EOF cleanup passed.')
