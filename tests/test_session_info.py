#!/usr/bin/env python3
"""Actual API usage, background conversation statistics, snapshot UI and restart."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_image_viewer import fixture as photo_fixture,shot
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))

class Server(http.server.BaseHTTPRequestHandler):
    requests=[];mode='usage';release=threading.Event()
    def log_message(self,*args):pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append(body);mode=self.mode;chat=self.path.endswith('/chat/completions')
        if chat:assert body['stream_options']=={'include_usage':True}
        self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
        def emit(value):
            data=('data: '+(value if isinstance(value,str) else json.dumps(value,ensure_ascii=False))+'\n\n').encode()
            for n in range(0,len(data),7):self.wfile.write(data[n:n+7]);self.wfile.flush()
        try:
            emit({'choices':[{'delta':{'content':'公开回复。'}}]} if chat else {'type':'response.output_text.delta','delta':'公开回复。'})
            if mode=='hold':self.release.wait(20)
            usage={'prompt_tokens':1000,'completion_tokens':200,'total_tokens':1200,
                   'prompt_tokens_details':{'cached_tokens':600,'cache_write_tokens':100},
                   'completion_tokens_details':{'reasoning_tokens':50}} if chat else {
                   'input_tokens':1000,'output_tokens':200,'total_tokens':1200,
                   'input_tokens_details':{'cached_tokens':600,'cache_write_tokens':100},
                   'output_tokens_details':{'reasoning_tokens':50}}
            usage['private']='USAGE_SECRET_MUST_NOT_PERSIST'
            if chat:
                emit({'choices':[{'delta':{},'finish_reason':'stop'}]})
                if mode!='absent':
                    emit({'choices':[],'usage':usage});emit({'choices':[],'usage':usage})
                emit('[DONE]')
            else:
                final={'type':'response.completed','response':{'output':[{'type':'message','content':[{'type':'output_text','text':'公开回复。'}]}],'usage':None if mode=='absent' else usage}}
                emit(final);emit(final)
        except (BrokenPipeError,ConnectionResetError):pass
        self.close_connection=True

def fixture(folder,port=1,api='responses',presets=False):
    photo_fixture(folder,port=port)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('update providers set metadata_json=?',(json.dumps({'api':api}),))
        db.execute("insert or replace into app_settings values('presets_enabled',?,1)",(json.dumps(presets),))

def open_info(a):
    a.attachment_action('session_info')
    t.wait_for(lambda:a.diag()['view']=='session_info' and bool(a.diag()['session_info']) and not a.diag()['session_info_busy'])
    data=a.diag()['session_info'];assert 'error' not in data,data
    return data

def run(api,w,h):
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-info-') as directory:
        threading.Thread(target=server.serve_forever,daemon=True).start();folder=pathlib.Path(directory)
        fixture(folder,server.server_port,api,api=='chat');Server.mode='usage';Server.requests=[]
        a=t.Native(BINARY,folder,screen={'width':w,'height':h,'direction':0})
        try:
            a.edit('保留草稿');before=a.diag();initial=open_info(a)
            assert initial['budget']==16384 and initial['images']==1 and initial['messages']==2
            assert initial['reported_replies']==0 and not initial['latest']
            assert a.diag()['draft_length']==4 and a.diag()['session']==before['session']
            assert 'test-only' not in json.dumps(initial)
            shot(a,f'info-{api}-{w}-empty-usage');a.tap(40,32)
            assert a.diag()['scroll_y']==before['scroll_y'] and a.diag()['draft_length']==4
            a.tap(w-52,h-34);t.wait_for(lambda:len(Server.requests)==1 and not a.diag()['generating'] and not a.diag()['compressing'])
            data=open_info(a)
            assert data['latest']=={'input_tokens':1000,'output_tokens':200,'total_tokens':1200,'cached_tokens':600,'cache_write_tokens':100,'reasoning_tokens':50},data
            assert data['totals']['total_tokens']==1200 and data['reported_replies']==1 and data['replies']==2
            assert data['cache_read_tokens']/data['cache_input_tokens']==.6
            assert data['latest_model']=='a'
            shot(a,f'info-{api}-{w}-top')
            for _ in range(3):a.settings_swipe(-130);time.sleep(.1)
            shot(a,f'info-{api}-{w}-usage')
            scroll=a.diag()['scroll_y'];a.page_action('刷新')
            t.wait_for(lambda:not a.diag()['session_info_busy'])
            assert a.diag()['session_info']['totals']['total_tokens']==1200
            for _ in range(6):a.settings_swipe(-130);time.sleep(.1)
            shot(a,f'info-{api}-{w}-notes')
            a.tap(40,32);a.edit('缺少用量的回复');Server.mode='absent';a.tap(w-52,h-34)
            t.wait_for(lambda:len(Server.requests)==2 and not a.diag()['generating'] and not a.diag()['compressing']);data=open_info(a)
            assert not data['latest'] and data['totals']['total_tokens']==1200 and data['reported_replies']==1
            a.tap(40,32);a.edit('生成时打开信息');Server.mode='hold';Server.release.clear();a.tap(w-52,h-34)
            t.wait_for(lambda:a.diag()['generating']);data=open_info(a)
            assert data['latest_status']=='streaming' and not data['latest']
            Server.release.set()
            t.wait_for(lambda:not a.diag()['generating'] and not a.diag()['session_info_busy'] and a.diag()['session_info'].get('reported_replies')==2)
            assert a.diag()['view']=='session_info' and a.diag()['session_info']['totals']['total_tokens']==2400
            assert len(Server.requests)==3
            with sqlite3.connect(folder/'penchat.db') as db:
                stored=db.execute("select metadata_json from chat_messages where role='assistant' order by seq desc limit 1").fetchone()[0]
                assert json.loads(stored)['usage']['cached_tokens']==600 and 'USAGE_SECRET' not in stored
            # A result arriving after leaving never switches the screen back.
            a.page_action('刷新');a.tap(40,32);time.sleep(.25);assert a.diag()['view']=='chat'
        finally:Server.release.set();a.close()
        b=t.Native(BINARY,folder,screen={'width':w,'height':h,'direction':0})
        try:
            data=open_info(b);assert data['totals']['total_tokens']==2400 and data['reported_replies']==2
        finally:b.close();server.shutdown()

def blank():
    with tempfile.TemporaryDirectory(prefix='penchat-info-blank-') as directory:
        t.port=1;a=t.Native(BINARY,pathlib.Path(directory),seed_model=False)
        try:
            data=open_info(a);assert data['budget'] is None and data['messages']==0
            assert a.diag()['summary']['sessions']==0
            shot(a,'info-no-model');a.tap(40,32);assert a.diag()['view']=='chat'
        finally:a.close()

if __name__=='__main__':
    blank();run('responses',960,266);run('chat',936,280)
    print('Session information: menu/preset coexistence, local estimate/budget, API usage/cache/reasoning, duplicate finals, missing counts, stream final refresh, late results, draft/reader preservation and restart passed at both sizes.')
