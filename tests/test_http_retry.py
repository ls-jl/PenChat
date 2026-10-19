#!/usr/bin/env python3
"""HTTP failures remain readable/retryable, including image requests and restarts."""
import base64,http.server,json,os,pathlib,sqlite3,tempfile,threading,time
from test_lvgl_app import Native,ROOT,wait_for
import test_lvgl_app as transport
class Handler(http.server.BaseHTTPRequestHandler):
    requests=[]
    mode='unicode'
    def log_message(self,*args):pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append(body)
        if self.mode in ('unicode','invalid'):
            data=('错误说明'*300).encode() if self.mode=='unicode' else b'bad gateway \xff\xfe'
            self.send_response(500);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        else:
            data=('data: '+json.dumps({'type':'response.output_text.delta','delta':'Success'})+'\n\n')
            if self.mode=='ok':data+='data: '+json.dumps({'type':'response.completed','response':{'output':[]}})+'\n\n'
            data=data.encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
transport.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start()
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
with tempfile.TemporaryDirectory(prefix='penchat-retry-') as directory:
    p=pathlib.Path(directory);app=Native(binary,p);app.close()
    photo=p/'scan.png';photo.write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aX1cAAAAASUVORK5CYII='))
    with sqlite3.connect(p/'penchat.db') as db:
        db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('retry','scan','active',1,1,1)")
        db.execute("insert into app_settings(key,value_json,updated_at) values('active_session_id','\"retry\"',1)")
        db.execute("insert into app_settings(key,value_json,updated_at) values('auto_title_enabled','false',1)")
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('image','retry',1,'user','complete','image','',1,1)")
        db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,created_at) values('asset','retry','image','image','scan',?,?,1)",(str(photo),str(photo)))
        db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('part','retry','image',0,'image','asset')")
    def messages():
        with sqlite3.connect(p/'penchat.db') as db:
            db.row_factory=sqlite3.Row
            return [dict(row) for row in db.execute('select * from chat_messages order by seq')]
    def status(app,value):
        try: wait_for(lambda:messages()[-1]['status']==value)
        except Exception:
            print('Requests:',len(Handler.requests),'Rows:',[(m['role'],m['status'],m['error_json']) for m in messages()],'State:',app.diag(),'Log:',(p/'runtime.log').read_text(),flush=True)
            raise
        assert app.process.poll() is None
    app=Native(binary,p)
    try:
        app.tap(908,232);status(app,'error');time.sleep(.15)
        first=messages()[-1]['id'];assert len(Handler.requests)==1
        assert app.diag()['modal']
        Handler.mode='invalid';app.tap(770,225);status(app,'error');time.sleep(.15)
        assert len(Handler.requests)==2 and len(messages())==2
        assert messages()[-1]['id']==first
        app.tap(890,225)
    finally:app.close()
    with sqlite3.connect(p/'penchat.db') as db:
        db.execute("update chat_messages set status='streaming' where role='assistant'")
    app=Native(binary,p)
    try:
        assert messages()[-1]['status']=='interrupted' and len(Handler.requests)==2
        Handler.mode='cut';app.tap(908,232);status(app,'error');time.sleep(.15)
        assert len(Handler.requests)==3
        Handler.mode='ok';app.tap(770,225);status(app,'complete')
        assert len(Handler.requests)==4 and len(messages())==2
        assert not app.diag()['modal']
        assert messages()[-1]['id']==first
        for request in Handler.requests:
            assert request['input']==Handler.requests[0]['input'],request
        assert 'data:image/png;base64,' in json.dumps(Handler.requests[0])
        assert photo.exists()
    finally:app.close()
server.shutdown()
print('HTTP retry: truncated Chinese/non-UTF-8 errors stay alive; manual retry/restart reuse images and message ID; partial replies excluded; no automatic retry.')
