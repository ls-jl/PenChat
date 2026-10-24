#!/usr/bin/env python3
"""Actual-process assistant copy, destructive retry and prefix branching."""
import http.server,json,os,pathlib,signal,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_reasoning_ui import Server
from test_message_edit import fixture as base_fixture
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/reply-actions';OUT.mkdir(parents=True,exist_ok=True)

def fixture(p,port):
    base_fixture(p,port)
    prefs={'model_id':'b','effort':'max','system_prompt':'CURRENT_SYSTEM_PROMPT','preset_id':'fixture-preset'}
    with sqlite3.connect(p/'penchat.db') as db:
        db.execute("update chat_sessions set metadata_json=?,default_provider_id='p',default_model_config_id='b' where id='s'",(json.dumps({'chat_preferences':prefs,'context_compression':{'version':1,'enabled':True,'through':7,'summary':'FORBIDDEN_FUTURE_SUMMARY','checksum':'invalid'}}),))
        db.execute("update chat_messages set output_text='**Old answer** $x^2$',reasoning_text='PUBLIC_THOUGHT_NOT_COPIED',provider_id='p',model_config_id='a',request_json=? where id='a2'",(json.dumps({'model':'a','effort':'medium','system_prompt':'OLD_SYSTEM_PROMPT'}),))
        db.execute("insert into app_settings values('message_edit_default','\"overwrite\"',1)")

def records(p):
    with sqlite3.connect(p/'penchat.db') as db:
        return db.execute('select id,session_id,seq,role,status,output_text from chat_messages order by session_id,seq').fetchall()

def shot(a,name):
    p=a.directory/'lvgl-diagnostic.png'
    if p.exists():p.unlink()
    a.process.send_signal(signal.SIGUSR1);t.wait_for(p.exists);(OUT/(name+'.png')).write_bytes(p.read_bytes())

def click(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)

def reveal(a,id,key):
    for _ in range(30):
        s=a.diag();row=s['reply_actions']['rows'].get(id)
        if not row:raise AssertionError(('reply not rendered',id,s))
        b=row[key]
        if 4<=b[1] and b[3]<s['height']-58:return b
        a.settings_swipe(100 if b[1]<4 else -100);time.sleep(.2)
    raise AssertionError(('Cannot reveal action',id,key,a.diag()))

def action(a,id,key):click(a,reveal(a,id,key));time.sleep(.1)
def bottom(a):
    s=a.diag()
    if s['to_bottom_visible']:a.tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
    t.wait_for(lambda:a.diag()['toolbar_y']==s['height']-58)

def confirm(a,archive):
    s=a.diag();w,h=s['width'],s['height'];assert s['reply_actions']['confirm']
    width=(w-262)//2;a.tap(84+14+(0 if archive else width+12)+width//2,h-44)

def select_history(a,id):
    bottom(a);a.open_navigation();s=a.diag();row=next(r for r in s['history_rows'] if r['id']==id);a.tap(row['x'],row['y']);t.wait_for(lambda:a.diag()['session']==id and not a.diag()['navigation_open'])

def read_clipboard(a):
    bottom(a);s=a.diag();w,h=s['width'],s['height'];a.tap(w//2,h-34)
    t.wait_for(lambda:a.diag()['editor']['active']);a.tap(w-30,h-30)
    t.wait_for(lambda:a.diag()['editor']['clipboard_open']);a.tap(w-240,100)
    r=a.receive('keyboard.open');value=r['text']
    a.send('keyboard.result',requestId=r['requestId'],confirmed=False,text='')
    t.wait_for(lambda:not a.diag()['editor']['active']);return value

def run_retry(w,h,api):
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-reply-ui-') as tmp:
        t.port=server.server_port;Server.requests=[];Server.mode='summary';threading.Thread(target=server.serve_forever,daemon=True).start()
        p=pathlib.Path(tmp);fixture(p,server.server_port)
        if api=='chat':
            with sqlite3.connect(p/'penchat.db') as db:db.execute('update providers set metadata_json=?',(json.dumps({'api':'chat'}),))
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        try:
            old=records(p);controls=a.diag()['reply_actions']['rows']['a2']
            assert len({controls[k][1] for k in ('copy','retry','branch')})==1
            assert controls['copy'][2]<controls['retry'][0]<controls['branch'][0]
            assert all(controls[k][3]-controls[k][1]+1>=44 for k in ('copy','retry','branch'))
            action(a,'a2','copy');assert read_clipboard(a)=='**Old answer** $x^2$'
            b=reveal(a,'a2','branch');before=a.diag()['editor']['clipboard_count'];a.long_press((b[0]+b[2])//2,(b[1]+b[3])//2)
            assert not a.diag()['reply_actions']['busy'] and a.diag()['summary']['sessions']==1
            action(a,'a2','retry');assert a.diag()['reply_actions']['confirm']=='a2'
            shot(a,f'{w}-{api}-retry-confirm');a.tap(w-70,h-44);assert records(p)==old and not Server.requests
            bottom(a);a.edit('UNSENT_DRAFT');action(a,'a2','retry');assert a.diag()['reply_actions']['confirm']=='a2'
            confirm(a,True)
            t.wait_for(lambda:bool(Server.requests) and not a.diag()['generating'] and not a.diag()['compressing'])
            s=a.diag();assert s['draft_length']==len('UNSENT_DRAFT') and not s['modal']
            body=Server.requests[-1][1];wire=json.dumps(body)
            assert body['model']=='b' and body.get('reasoning_effort',body.get('reasoning',{}).get('effort'))=='max'
            assert 'CURRENT_SYSTEM_PROMPT' in wire and 'OLD_SYSTEM_PROMPT' not in wire
            assert 'UNSENT_DRAFT' not in wire and 'Later question' not in wire and 'FORBIDDEN_FUTURE_SUMMARY' not in wire and '**Old answer**' not in wire
            images=wire.count('data:image/png;base64,');assert images==1,(images,body)
            with sqlite3.connect(p/'penchat.db') as db:
                archive=db.execute("select id from chat_sessions where status='archived'").fetchone();assert archive
                assert db.execute('select count(*) from chat_messages where session_id=?',archive).fetchone()[0]==8
                live=db.execute("select id,seq,role,output_text from chat_messages where session_id='s' order by seq").fetchall()
                assert len(live)==6 and live[-1][0]=='pending' and live[-2][0]=='a2'
                assert live[-1][1]>live[-2][1] and not any(r[0] in ('u3','a3') for r in live)
                assert db.execute('pragma foreign_key_check').fetchall()==[]
            shot(a,f'{w}-{api}-regenerated');count=len(Server.requests)
            action(a,'a2','retry');confirm(a,False)
            t.wait_for(lambda:len(Server.requests)>count and not a.diag()['generating'] and not a.diag()['compressing'])
            with sqlite3.connect(p/'penchat.db') as db:assert db.execute("select count(*) from chat_sessions where status='archived'").fetchone()[0]==1
            assert a.diag()['draft_length']==len('UNSENT_DRAFT')
            assert (p/'original.png').exists()
        finally:a.close();server.shutdown()

def run_branch(w,h):
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-branch-ui-') as tmp:
        t.port=server.server_port;Server.requests=[];threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);fixture(p,server.server_port)
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        try:
            a.edit('MOVE_DRAFT');action(a,'a2','branch');t.wait_for(lambda:a.diag()['session']!='s' and not a.diag()['reply_actions']['busy'])
            branch=a.diag()['session'];assert a.diag()['draft_length']==len('MOVE_DRAFT') and not Server.requests
            prefs=a.diag()['session_preferences'];assert prefs['model_id']=='b' and prefs['effort']=='max' and prefs['system_prompt']=='CURRENT_SYSTEM_PROMPT'
            with sqlite3.connect(p/'penchat.db') as db:
                title,meta,pin=db.execute('select title,metadata_json,pinned_at from chat_sessions where id=?',(branch,)).fetchone();meta=json.loads(meta)
                assert title=='编辑测试 · 分支' and not pin and 'context_compression' not in meta
                assert db.execute("select count(*) from chat_messages where session_id='s'").fetchone()[0]==7
                rows=db.execute('select id,role,output_text from chat_messages where session_id=? order by seq',(branch,)).fetchall()
                assert len(rows)==6 and rows[-2][2]=='**Old answer** $x^2$'
                assert db.execute('select id from model_configs where is_default=1').fetchone()[0]=='a'
                assert db.execute('pragma foreign_key_check').fetchall()==[]
            shot(a,f'{w}-branch-created')
            select_history(a,'s');assert a.diag()['draft_length']==0
            select_history(a,branch);assert a.diag()['draft_length']==len('MOVE_DRAFT')
            target=rows[-2][0];action(a,target,'branch');t.wait_for(lambda:a.diag()['session']!=branch and not a.diag()['reply_actions']['busy'])
            with sqlite3.connect(p/'penchat.db') as db:
                name=db.execute('select title from chat_sessions where id=?',(a.diag()['session'],)).fetchone()[0];assert name=='编辑测试 · 分支2'
                assert db.execute('select count(*) from chat_messages where session_id=?',(branch,)).fetchone()[0]==5
            assert a.diag()['draft_length']==len('MOVE_DRAFT') and not Server.requests
            shot(a,f'{w}-branch2')
        finally:a.close();server.shutdown()

def preflight_failure():
    with tempfile.TemporaryDirectory(prefix='penchat-retry-preflight-') as tmp:
        p=pathlib.Path(tmp);fixture(p,1);(p/'assets/photo.png').unlink();a=t.Native(BINARY,p)
        try:
            before=records(p);action(a,'a2','retry');confirm(a,True)
            t.wait_for(lambda:not a.diag()['compressing']);assert records(p)==before and a.diag()['modal']
        finally:a.close()

def stream_and_empty_cases():
    gate=threading.Event()
    class Stream(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_POST(self):
            self.rfile.read(int(self.headers['Content-Length']))
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.end_headers()
            events=[{'type':'response.output_item.added','output_index':0,'item':{'id':'r','type':'reasoning'}},
                    {'type':'response.reasoning_summary_text.delta','output_index':0,'summary_index':0,'delta':'DO_NOT_COPY_THOUGHT'},
                    {'type':'response.output_text.delta','output_index':1,'delta':'STREAM_PART'}]
            try:
                for e in events:self.wfile.write(('data: '+json.dumps(e)+'\n\n').encode())
                self.wfile.flush();gate.wait(20)
                self.wfile.write(b'data: {"type":"response.output_text.delta","output_index":1,"delta":"_FINAL"}\n\ndata: {"type":"response.completed"}\n\n');self.wfile.flush()
            except BrokenPipeError:pass
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-reply-stream-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp)
        from test_reasoning_ui import seed
        seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        a=t.Native(BINARY,p)
        try:
            a.edit('stream test');a.tap(908,232)
            t.wait_for(lambda:any(c['copy_enabled'] for c in a.diag()['reply_actions']['rows'].values()))
            id=next(id for id,c in a.diag()['reply_actions']['rows'].items() if c['copy_enabled'])
            c=a.diag()['reply_actions']['rows'][id];assert not c['retry_enabled'] and not c['branch_enabled']
            action(a,id,'copy');assert read_clipboard(a)=='STREAM_PART'
            shot(a,'streaming-copy-disabled-actions');gate.set();t.wait_for(lambda:not a.diag()['generating'])
            bottom(a);shot(a,'short-reply-with-toolbar')
        finally:gate.set();a.close();server.shutdown()
    with tempfile.TemporaryDirectory(prefix='penchat-reference-actions-') as tmp:
        p=pathlib.Path(tmp);fixture(p,1)
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute('delete from message_parts');db.execute('delete from message_assets');db.execute('delete from chat_messages')
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values('ref','s',1,'assistant','complete','text',1,1,'导入的参考内容')")
        a=t.Native(BINARY,p)
        try:
            c=a.diag()['reply_actions']['rows']['ref'];assert c['copy_enabled'] and not c['retry_enabled'] and c['branch_enabled']
            shot(a,'reference-no-retry')
        finally:a.close()

def restart_cases():
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-reply-restart-') as tmp:
        t.port=server.server_port;Server.requests=[];Server.mode='none';threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);fixture(p,server.server_port)
        a=t.Native(BINARY,p)
        try:
            action(a,'a2','branch');t.wait_for(lambda:a.diag()['session']!='s' and not a.diag()['reply_actions']['busy']);branch=a.diag()['session']
        finally:a.close()
        b=t.Native(BINARY,p)
        try:
            assert b.diag()['session']==branch and b.diag()['session_preferences']['effort']=='max' and not Server.requests
            with sqlite3.connect(p/'penchat.db') as db:target=db.execute("select id from chat_messages where session_id=? and role='assistant' order by seq desc limit 1",(branch,)).fetchone()[0]
            action(b,target,'retry');confirm(b,False);t.wait_for(lambda:bool(Server.requests) and not b.diag()['generating'] and not b.diag()['compressing'])
        finally:b.close()
        c=t.Native(BINARY,p)
        try:
            assert c.diag()['session']==branch and len(Server.requests)==1
            with sqlite3.connect(p/'penchat.db') as db:
                value=db.execute('select status,output_text from chat_messages where id=?',(target,)).fetchone();assert value==('complete','回答完成。'),value
                assert db.execute('pragma foreign_key_check').fetchall()==[]
                assert db.execute('select count(*) from message_assets where session_id=?',(branch,)).fetchone()[0]==2
        finally:c.close();server.shutdown()

if __name__=='__main__':
    for w,h in ((960,266),(936,280)):
        run_retry(w,h,'responses');run_branch(w,h)
    run_retry(960,266,'chat');preflight_failure();stream_and_empty_cases();restart_cases()
    print('Assistant actions: raw/stream copy, busy states, tooltips, reference handling, confirmation/cancel, archive/direct retry, frozen config, draft/photo isolation, preflight failure, prefix branches, numbering and moved draft restoration passed.')
