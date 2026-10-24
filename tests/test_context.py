#!/usr/bin/env python3
"""Actual-process context compaction: HTTP batches, atomic archives, cancellation.
All keys, providers, databases and images are isolated fixtures. No device access.
"""
import http.server, json, os, pathlib, sqlite3, tempfile, threading, time, struct, zlib, subprocess, shutil, zipfile
from test_lvgl_app import Native, ROOT, wait_for
from test_reasoning_ui import seed
from test_chat_style import drag, snapshot
import test_lvgl_app as transport

class Server(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    requests=[]
    delay=0
    fail=False
    verbose=False
    def log_message(self,*args): pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append((self.path,body))
        compression='PenChat_CONTEXT_COMPRESSION_V1' in json.dumps(body)
        if compression and self.fail:
            self.send_response(500);self.send_header('Content-Length','0');self.end_headers();return
        self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
        try:
            if compression: time.sleep(self.delay)
            text=('事实摘要：保留约束、决定和图片信息。' if not self.verbose else '过长摘要'*1000) if compression else '正文完成。'
            chat=self.path.endswith('/chat/completions')
            payload={'choices':[{'delta':{'content':text}}]} if chat else {'type':'response.output_text.delta','delta':text}
            # Exercise fragmented multibyte SSE for the separate compression reader too.
            wire=('data: '+json.dumps(payload,ensure_ascii=False)+'\n\n').encode()
            for i in range(0,len(wire),7):self.wfile.write(wire[i:i+7])
            final='[DONE]' if chat else '{"type":"response.completed"}'
            self.wfile.write(('data: '+final+'\n\n').encode());self.wfile.flush()
        except (BrokenPipeError,ConnectionResetError): pass
        self.close_connection=True

def fixture(p,port,rounds=16,budget=4096,compression_budget=8192,api='responses',auto=True,image=False):
    seed(p,port)
    with sqlite3.connect(p/'penchat.db') as db:
        db.execute('update providers set metadata_json=?',(json.dumps({'api':api,'default_effort':'high'}),))
        for mid,b in [('a',budget),('b',compression_budget)]:
            db.execute('update model_configs set capabilities_json=? where id=?',(json.dumps({'context_budget':b,'reasoning':{'levels':{'high':'high'},'summary':True}}),mid))
        prefs={'model_id':'a','system_prompt':'CHAT_SYSTEM_ONCE','compression_model_id':'b','has_sent':True,'effort':'high'}
        db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at,message_count,metadata_json,default_model_config_id,default_provider_id) values('s','上下文测试','active',1,1,1,?,?, 'a','p')",(rounds*2,json.dumps({'chat_preferences':prefs})))
        for i in range(rounds*2):
            text=f'fact-{i}: '+('reference information '*20)
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text,metadata_json,provider_id,model_config_id) values(?,'s',?,?,'complete','text',1,1,?,?,'p','a')",(f'm{i}',i+1,'user' if i%2==0 else 'assistant',text,json.dumps({'response_reasoning_items':[{'type':'reasoning','encrypted_content':'opaque-covered','status':'completed'}]} if i%2 else {})))
        if image and rounds:
            assets=p/'assets';assets.mkdir(exist_ok=True)
            path=assets/'fixture.png'
            def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
            path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',12,12,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([80,40,20])*12)*12))+chunk(b'IEND',b''))
            db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,stored_path,original_path,width,height,size_bytes,created_at) values('asset','s','m0','image','test',?,?,12,12,?,1)",(str(path),str(path),path.stat().st_size))
            db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('part','s','m0',0,'image','asset')")
        for key,val in [('active_session_id','s'),('auto_title_enabled',False),('auto_context_compression',auto)]:
            db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',(key,json.dumps(val)))

def wait_idle(app):
    wait_for(lambda:not app.diag()['compressing'] and not app.diag()['generating'],30)

def manual(app):
    app.to_chat();app.tap(128,232)
    drag(app,[190,155,125,95,65,35],x=250)
    # Six rows, scroll bottom 84px: compression is second to last.
    app.tap(240,124)

def checkpoint(p,s='s'):
    with sqlite3.connect(p/'penchat.db') as db:
        row=db.execute('select metadata_json from chat_sessions where id=?',(s,)).fetchone()
        return json.loads(row[0]).get('context_compression',{}) if row else {}

def transfer_roundtrip(p):
    driver=pathlib.Path(os.environ.get('PENCHAT_TRANSFER_TEST_BINARY',ROOT/'build/host/penchat-transfer-tests'))
    folder=p/'transfer-check'
    child=subprocess.Popen([str(driver),str(p),str(folder)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    def call(op,args={}):
        child.stdin.write(json.dumps({'op':op,'args':args})+'\n');child.stdin.flush()
        response=json.loads(child.stdout.readline());assert response['type']=='transfer_result',response
        return response['data']['data']
    try:
        call('init')
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute('insert or replace into app_settings(key,value_json,updated_at) values(?,?,1)',('chat_presets',json.dumps([{'id':'default','name':'默认预设','prompt_mode':'inherit','compression_model_id':'b'}])))
        package=pathlib.Path(call('export',{'kind':'chats','ids':['s']})['path'])
        shutil.copy2(package,folder/'导入/聊天'/package.name)
        preview=call('preview',{'kind':'chats','name':package.name});assert not preview['items'][0]['warning'],preview
        assert call('import',{'indices':[0],'copy':True})['added']==1
        with sqlite3.connect(p/'penchat.db') as db:
            rows=db.execute("select id,metadata_json from chat_sessions where status='active' and id!='s'").fetchall()
            copied=json.loads(rows[-1][1]);cp=copied['context_compression']
            assert cp.get('enabled',True) and cp['through']==checkpoint(p)['through'],cp
            assert 'archive_id' not in cp
            assert copied['chat_preferences']['compression_model_id']=='b'
        # A correctly packaged but stale coverage checksum is retained, disabled, and warned about.
        with zipfile.ZipFile(package) as z: entries={n:z.read(n) for n in z.namelist()}
        manifest=json.loads(entries['manifest.json']);name=manifest['items'][0]['file']
        content=json.loads(entries[name]);content['metadata']['context_compression']['checksum']='mismatch'
        entries[name]=json.dumps(content,ensure_ascii=False).encode()
        # Recompute package CRC to simulate a valid transport containing an invalid checkpoint.
        manifest['files'][name]['size']=len(entries[name])
        manifest['files'][name]['crc32']=format(zlib.crc32(entries[name])&0xffffffff,'08x')
        entries['manifest.json']=json.dumps(manifest,ensure_ascii=False).encode()
        target=folder/'导入/聊天/stale.penchat.zip'
        with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:
            for name,data in entries.items():z.writestr(name,data)
        stale=call('preview',{'kind':'chats','name':target.name})
        assert '停用摘要' in stale['items'][0]['warning']
        call('import',{'indices':[0],'copy':True})
        with sqlite3.connect(p/'penchat.db') as db:
            assert any(json.loads(m).get('context_compression',{}).get('enabled') is False for (m,) in db.execute('select metadata_json from chat_sessions'))
        presets=pathlib.Path(call('export',{'kind':'presets','all':True})['path'])
        shutil.copy2(presets,folder/'导入/预设'/presets.name)
        item=json.loads(presets.read_text())['items'][0]
        assert item['compression_model']['name']=='b' and 'api_key' not in json.dumps(item)
        call('preview',{'kind':'presets','name':presets.name})
        call('import',{'indices':[0],'default_action':'copy','copy':True})
        with sqlite3.connect(p/'penchat.db') as db:
            saved=json.loads(db.execute("select value_json from app_settings where key='chat_presets'").fetchone()[0])
            assert saved[-1]['compression_model_id']=='b'
    finally:
        child.stdin.close();assert child.wait(timeout=10)==0

def run():
    binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    transport.port=server.server_port
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        for api in ['responses','chat']:
            with tempfile.TemporaryDirectory(prefix='penchat-context-') as directory:
                p=pathlib.Path(directory);fixture(p,server.server_port,rounds=40,budget=16384,api=api,image=True)
                app=Native(binary,p)
                try:
                    Server.requests=[];Server.delay=.15
                    app.edit('CURRENT_PENDING');app.tap(908,232)
                    wait_for(lambda:app.diag()['compressing'])
                    snapshot(app,'context-progress-'+api)
                    wait_idle(app)
                    assert not app.diag()['modal'],(p/'runtime.log').read_text()
                    cp=checkpoint(p);assert cp['summary'] and cp['archive_id'] and cp['through']==72,cp
                    calls=[b for _,b in Server.requests if 'PenChat_CONTEXT_COMPRESSION_V1' in json.dumps(b)]
                    assert len(calls)>1,len(calls)
                    assert all(b['model']=='b' and 'reasoning_effort' not in b and 'reasoning' not in b for b in calls)
                    assert 'data:image/' in json.dumps(calls),calls[0]
                    answer=Server.requests[-1][1]
                    assert answer['model']=='a' and 'CURRENT_PENDING' in json.dumps(answer)
                    if api=='responses':
                        assert answer['instructions']=='CHAT_SYSTEM_ONCE'
                        assert answer['reasoning']['effort']=='high'
                    else:
                        assert answer['messages'][0]=={'role':'system','content':'CHAT_SYSTEM_ONCE'}
                        assert sum(m['role']=='system' for m in answer['messages'])==1
                    # Covered source/opaque data is absent; summary is ordinary historical data.
                    assert 'fact-0:' not in json.dumps(answer) and 'opaque-covered' not in json.dumps(calls)
                    with sqlite3.connect(p/'penchat.db') as db:
                        assert db.execute("select count(*) from chat_sessions where status='archived'").fetchone()[0]==1
                        assert db.execute("select count(*) from chat_messages where session_id=?",(cp['archive_id'],)).fetchone()[0]==80
                        assert db.execute("select count(*) from message_assets").fetchone()[0]==2
                    app.tap(40,168);snapshot(app,'context-summary-'+api);app.tap(890,222)
                    # Explicit second compression, exactly one more complete pre-state copy.
                    before=len(Server.requests);manual(app);wait_idle(app)
                    assert checkpoint(p)['revision']==2
                    with sqlite3.connect(p/'penchat.db') as db:
                        assert db.execute("select count(*) from chat_sessions where status='archived'").fetchone()[0]==2
                    snapshot(app,'context-summary-manual-'+api)
                    app.tap(890,222);app.open_settings();drag(app,[220,180,120,60],x=350)
                    snapshot(app,'context-settings-'+api)
                    app.to_chat();app.edit('ARCHIVE_KEEP');app.tap(40,168);app.tap(150,222)
                    assert app.diag()['view']=='archives' and app.diag()['session']=='s'
                    snapshot(app,'context-archives-'+api)
                    app.to_chat();assert app.diag()['draft_length']==12
                    app.open_settings();app.setting("models");app.tap(870,175)
                    assert app.diag()['view']=='model'
                    drag(app,[230,190,150,100,60],x=450)
                    snapshot(app,'context-model-budget-'+api)
                    app.page_action("保存");app.to_chat()
                    app.open_settings();app.setting("presets");app.tap(300,166)
                    assert app.diag()['view']=='preset_edit'
                    drag(app,[230,190,150,100,60],x=450)
                    snapshot(app,'context-preset-model-'+api)
                finally:app.close()
                transfer_roundtrip(p)
        # A selection made while compaction is running must survive the queued send.
        for api in ['responses','chat']:
            with tempfile.TemporaryDirectory(prefix='penchat-context-selection-') as d:
                p=pathlib.Path(d);fixture(p,server.server_port,rounds=40,budget=16384,api=api,image=True)
                app=Native(binary,p)
                try:
                    Server.requests=[];Server.delay=1
                    app.edit('selection');app.tap(908,232);wait_for(lambda:app.diag()['compressing'])
                    app.tap(890,222);app.open_settings();app.setting("models");app.tap(300,175)
                    assert app.diag()['active_model']=='b'
                    wait_idle(app)
                    assert app.diag()['active_model']=='b', 'Queued send replaced the next-request selection'
                    assert Server.requests[-1][1]['model']=='a'
                    app.to_chat();Server.delay=0;app.edit('next');app.tap(908,232);wait_idle(app)
                    assert Server.requests[-1][1]['model']=='b'
                finally:app.close();Server.delay=0
        # Disabled automatic mode sends every one of 102 historical rows, no silent 80 limit.
        with tempfile.TemporaryDirectory(prefix='penchat-context-all-') as d:
            p=pathlib.Path(d);fixture(p,server.server_port,rounds=51,budget=200000,auto=False)
            app=Native(binary,p)
            try:
                Server.requests=[];app.edit('ALL_HISTORY');app.tap(908,232);wait_idle(app)
                body=Server.requests[-1][1]
                assert 'fact-0:' in json.dumps(body) and 'fact-101:' in json.dumps(body)
                assert not checkpoint(p)
            finally:app.close()
        # Manual compaction also works on fewer than four turns and splits one very long reply.
        with tempfile.TemporaryDirectory(prefix='penchat-context-manual-') as d:
            p=pathlib.Path(d);fixture(p,server.server_port,rounds=1,budget=16384)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("update chat_messages set output_text=? where id='m1'",('长回复事实。'*3000,))
            app=Native(binary,p)
            try:
                Server.requests=[];app.edit('MANUAL_DRAFT');manual(app);wait_idle(app)
                assert checkpoint(p)['through']==2
                assert len(Server.requests)>1
                app.tap(890,222);app.tap(500,232);q=app.receive('keyboard.open');assert q['text']=='MANUAL_DRAFT'
            finally:app.close()
        with tempfile.TemporaryDirectory(prefix='penchat-context-empty-') as d:
            p=pathlib.Path(d);fixture(p,server.server_port,rounds=0)
            app=Native(binary,p)
            try:
                Server.requests=[];app.edit('ONLY_DRAFT');manual(app);wait_idle(app)
                assert not Server.requests and not checkpoint(p)
            finally:app.close()
        # HTTP error, too-long output, unavailable image capability and stop retain original state/draft.
        for mode in ['http','cancel','long','vision','model','switch','budget']:
            with tempfile.TemporaryDirectory(prefix='penchat-context-failure-') as d:
                p=pathlib.Path(d);fixture(p,server.server_port,image=True,auto=mode!='budget')
                if mode in ['vision','model']:
                    with sqlite3.connect(p/'penchat.db') as db:
                        if mode=='vision':db.execute("update model_configs set capabilities_json=? where id='b'",(json.dumps({'context_budget':4096,'vision':False}),))
                        else:db.execute("update model_configs set enabled=0 where id='b'")
                app=Native(binary,p)
                try:
                    Server.requests=[];Server.fail=mode=='http';Server.verbose=mode=='long';Server.delay=2 if mode in ['cancel','switch'] else 0
                    app.edit('KEEP_DRAFT');app.tap(908,232)
                    if mode in ['cancel','switch']:
                        wait_for(lambda:app.diag()['compressing'])
                        if mode=='cancel':app.tap(140,222)
                        else:
                            app.tap(890,222);app.open_navigation();app.tap(180,236)
                    wait_idle(app)
                    assert not checkpoint(p),mode
                    with sqlite3.connect(p/'penchat.db') as db:
                        assert db.execute('select count(*) from chat_sessions').fetchone()[0]==1,mode
                        assert db.execute("select count(*) from chat_messages where output_text='KEEP_DRAFT'").fetchone()[0]==0
                    if mode!='switch':
                        if app.diag()['modal']:app.tap(890,222)
                        app.tap(500,232);q=app.receive('keyboard.open');assert q['text']=='KEEP_DRAFT',mode
                finally:app.close();Server.fail=False;Server.verbose=False;Server.delay=0
    finally:server.shutdown()
    print('Context HTTP batches, image summaries, 102-row history, 2 APIs, archives, errors and cancellation passed.')
if __name__=='__main__':run()
