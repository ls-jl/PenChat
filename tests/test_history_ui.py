#!/usr/bin/env python3
"""History actions, independent one-shot naming and explicit API-default effort."""
import http.server, json, os, pathlib, sqlite3, tempfile, threading, time
from test_lvgl_app import Native, ROOT, wait_for
from test_reasoning_ui import seed, screenshot, gauge
import test_lvgl_app as transport

class Server(http.server.BaseHTTPRequestHandler):
    requests=[];titles=[];title_delay=0;fail_title=False;fail_answer=False;completed=0
    def log_message(self,*args):pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        naming='[penchat-title]' in json.dumps(body)
        (self.titles if naming else self.requests).append(body)
        fail=self.fail_title if naming else self.fail_answer
        if fail:
            payload=b'{"error":{"message":"fixture failure"}}'
            self.send_response(500);self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
        try:
            if naming:
                assert self.completed>0
                time.sleep(self.title_delay)
                text='标题：“首轮问答生成的完整标题以及故意多出来的文字用于验证空间限制”\n不要显示第二行'
            else:
                time.sleep(.12);text='这是首个问题的完整公开答复。'
            self.wfile.write(('data: '+json.dumps({'type':'response.output_text.delta','delta':text},ensure_ascii=False)+'\n\n').encode());self.wfile.flush()
            if not naming:
                time.sleep(.15);type(self).completed+=1
            self.wfile.write(b'data: {"type":"response.completed"}\n\n');self.wfile.flush()
        except (BrokenPipeError,ConnectionResetError):pass
        self.close_connection=True

def query(directory,sql,args=()):
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:return db.execute(sql,args).fetchall()
def info(directory,id):
    rows=query(directory,'select title,status,metadata_json from chat_sessions where id=?',(id,))
    return (rows[0][0],rows[0][1],json.loads(rows[0][2])) if rows else None
def context(app,id):
    if not app.diag()['navigation_open']:app.open_navigation()
    row=next(r for r in app.diag()['history_rows'] if r['id']==id)
    app.long_press(row['x'],row['y']);assert app.diag()['history_context']==id

def new(app):
    app.open_navigation();app.tap(180,236);return app.diag()['session']
def send(app,text):
    app.edit(text);app.tap(908,232);wait_for(lambda:not app.diag()['generating'])
def named(app,directory,id):
    wait_for(lambda:info(directory,id)[2].get('title_state')=='complete',timeout=6)
    assert len(info(directory,id)[0])<=app.diag()['title_limit']

def run():
    binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-history-') as directory:
        transport.port=server.server_port;seed(directory,server.server_port)
        with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
            for n in range(2):
                id='old'+str(n)
                db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,'active',1,?,1)",(id,'旧会话'+str(n),n+1))
                db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values(?,?,1,'assistant','complete','text',1,1,'旧的完整答复')",('reply'+str(n),id))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        app=Native(binary,pathlib.Path(directory))
        try:
            assert not Server.titles  # No retroactive API calls for old history.
            first=app.diag()['session'];app.edit('第一个问题');app.tap(908,232)
            time.sleep(.08);assert not Server.titles
            wait_for(lambda:not app.diag()['generating']);named(app,directory,first)
            assert len(Server.titles)==1 and Server.titles[-1]['model']=='a'
            assert 'effort' not in Server.titles[-1].get('reasoning',{})
            app.open_navigation();d=app.diag();assert abs(d['history_rows'][0]['y']-30)<=1, d['history_rows']
            assert d['title_width']<=568*.8 and d['title_limit']<=23
            screenshot(app,'history-no-header')
            context(app,first);screenshot(app,'history-actions');app.tap(250,147)
            assert info(directory,first)[2]['pinned_at']>0
            context(app,'old0');app.tap(250,147)
            assert app.diag()['history_rows'][0]['id']=='old0'
            context(app,'old0');app.tap(110,147);request=app.receive('keyboard.open')
            app.send('visibility',visible=False)
            app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='手动置顶标题')
            app.send('visibility',visible=True)
            wait_for(lambda:app.diag()['navigation_open'])
            assert info(directory,'old0')[0]=='手动置顶标题'
            context(app,'old0');app.tap(390,147)
            assert info(directory,'old0')[1]=='archived' and not any(r['id']=='old0' for r in app.diag()['history_rows'])
            app.tap(500,236);app.tap(740,80);assert app.diag()['view']=='archives';screenshot(app,'archives')
            app.tap(726,76);assert info(directory,'old0')[1]=='active'
            app.to_chat();app.open_navigation();assert app.diag()['history_rows'][0]['id']=='old0'
            context(app,'old0');app.tap(390,147);app.tap(500,236);app.tap(740,80);app.tap(846,76)
            assert info(directory,'old0') is None
            app.tap(40,32);app.tap(300,192);assert app.diag()['popup']=='title_models'
            app.tap(300,162);assert app.diag()['active_model']=='a'
            assert json.loads(query(directory,"select value_json from app_settings where key='title_model_id'")[0][0])=='b'
            app.to_chat();second=new(app);send(app,'指定模型命名');named(app,directory,second)
            assert Server.titles[-1]['model']=='b' and Server.requests[-1]['model']=='a'
            count=len(Server.titles);old_title=info(directory,second)[0]
            send(app,'追问不再次命名');time.sleep(.15);assert len(Server.titles)==count and info(directory,second)[0]==old_title
            gauge(app);assert app.diag()['effort_choices']==['none','api_default','medium','high','xhigh']
            screenshot(app,'default-effort-slot');app.tap(334,110);app.tap(800,230)
            assert app.diag()['effective_effort']=='api_default';send(app,'使用API默认')
            assert 'effort' not in Server.requests[-1].get('reasoning',{})
            gauge(app);app.tap(146,110);app.tap(800,230);send(app,'使用即时')
            assert Server.requests[-1]['reasoning']['effort']=='none'
            Server.title_delay=2
            pending=new(app);send(app,'命名过程中手动改名')
            wait_for(lambda:app.diag()['title_busy'])
            context(app,pending);app.tap(110,147);request=app.receive('keyboard.open')
            app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='用户确认的标题')
            wait_for(lambda:not app.diag()['title_busy'],timeout=6);time.sleep(.1)
            assert info(directory,pending)[0]=='用户确认的标题' and info(directory,pending)[2]['title_state']=='manual'
            removed=new(app);send(app,'命名过程中删除会话');wait_for(lambda:app.diag()['title_busy'])
            context(app,removed);app.tap(530,147)
            wait_for(lambda:not app.diag()['title_busy'],timeout=6)
            assert info(directory,removed) is None
            Server.title_delay=0;Server.fail_title=True
            failed=new(app);send(app,'标题请求失败保留这个摘要')
            wait_for(lambda:info(directory,failed)[2].get('title_state')=='failed')
            assert info(directory,failed)[0]=='标题请求失败保留这个摘要' and not app.diag()['modal']
            before=len(Server.titles);send(app,'失败后不自动重复请求');time.sleep(.15);assert len(Server.titles)==before
            Server.fail_title=False;Server.fail_answer=True
            incomplete=new(app);send(app,'回复未完成不命名');assert app.diag()['modal']
            assert not info(directory,incomplete)[2].get('title_state') and len(Server.titles)==before
        finally:app.close()
        server.shutdown()
    print('History UI: header-free rows, long press, pin/rename/archive/restore/delete, first-answer naming, model override, title limits, late-result guards and API-default slot passed.')
if __name__=='__main__':run()
