"""Isolated physical-touch compatibility acceptance, no real keys or external API."""
import http.server,json,pathlib,re,shutil,socket,sqlite3,subprocess,sys,threading,time
class Server(http.server.BaseHTTPRequestHandler):
    requests=[];mode='json'
    def log_message(self,*args):pass
    def do_POST(self):
        assert self.request_version=='HTTP/1.1'
        assert self.headers.get('X-PenChat-Fixture')=='local'
        assert self.headers.get_all('Accept')==['application/json']
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append((self.path,body))
        if self.mode=='error':status=401;value={'error':{'message':'fixture key rejected'}}
        elif self.path.endswith('/chat/completions'):
            status=200;value={'choices':[{'index':0,'message':{'role':'assistant','content':'兼容模式回复。'},'finish_reason':'stop'}],
                              'usage':{'prompt_tokens':1000,'completion_tokens':200,'total_tokens':1200}}
        else:
            status=200;value={'object':'response','status':'completed','output':[{'type':'message','role':'assistant','phase':'final_answer','status':'completed',
                  'content':[{'type':'output_text','text':'兼容模式回复。'}]}],'usage':{'input_tokens':1000,'output_tokens':200,'total_tokens':1200}}
        data=json.dumps(value,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type','text/plain');self.send_header('Content-Length',str(len(data)));self.send_header('x-request-id','device-fixture');self.end_headers();self.wfile.write(data)
def prepare(root,folder,data,serial):
    sys.path.insert(0,str(root/'tests'))
    from test_session_info import fixture
    route=subprocess.check_output(['adb','-s',serial,'shell','ip -4 route show default'],text=True)
    ip=re.search(r'\bsrc (\d+\.\d+\.\d+\.\d+)',route).group(1)
    connection=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);connection.connect((ip,9));host=connection.getsockname()[0];connection.close()
    Server.requests=[];Server.mode='json';server=http.server.ThreadingHTTPServer((host,0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();api='chat' if serial.endswith('113') else 'responses';fixture(folder,server.server_port,api)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('update providers set base_url=?,metadata_json=?,headers_json=?',('http://'+host+':'+str(server.server_port)+'/v1',
                   json.dumps({'api':api,'default_effort':'medium','compatibility':dict(enabled=True,usage=False,summary=False,replay=False,http1=True)}),json.dumps({'X-PenChat-Fixture':'local','accept':'application/json'})))
        db.execute('update message_assets set stored_path=?,original_path=?',(data+'/assets/original.png',)*2)
        for key in ('auto_title_enabled','auto_context_compression'):db.execute('insert or replace into app_settings values(?,?,1)',(key,'false'))
    return server,Server,[(folder/'penchat.db',data+'/penchat.db'),(folder/'assets/original.png',data+'/assets/original.png')]
def run(ctx,handler):
    read,tap,wait,capture=map(ctx.get,('state','tap','wait','capture'));W,H=ctx['W'],ctx['H']
    def button(bounds):tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
    def type_question():
        tap(W//2,H-34);wait(lambda:read()['editor']['active'])
        if read()['editor']['chinese']:ctx['key']('language')
        ctx['key']('q');button(read()['editor']['actions']['done']);wait(lambda:not read()['editor']['active'])
    def settle(count):wait(lambda:len(handler.requests)==count and not read()['generating'] and not read()['compressing'] and not read()['storage']['unsaved'],timeout=20)
    def attachment(key):
        tap(128,H-34)
        for _ in range(20):
            s=read();b=s['attachment_actions'][key];v=s['attachment_viewport']
            if b[1]>=v[1] and b[3]<=v[3]:button(b);return
            ctx['drag'](220,v[3]-10,220,max(v[1]+10,v[3]-110))
        raise AssertionError(key)
    type_question();tap(W-52,H-34);settle(1);assert not read()['modal'];capture('api-compatible-json-chat')
    body=handler.requests[-1][1];assert body['stream'] is True and 'stream_options' not in body and 'include' not in body
    if 'reasoning' in body:assert 'summary' not in body['reasoning']
    assert 'data:image/png;base64,' in json.dumps(body)
    attachment('session_info');wait(lambda:bool(read()['session_info']) and not read()['session_info_busy'])
    assert read()['session_info']['latest']['total_tokens']==1200;capture('api-compatible-usage');tap(40,32)
    handler.mode='error';type_question();tap(W-52,H-34);settle(2);wait(lambda:read()['modal']);capture('api-http-error')
    time.sleep(.7);assert len(handler.requests)==2
    handler.mode='json';tap(W-190,H-44);wait(lambda:bool(read()['reply_actions']['confirm']));capture('api-manual-retry-confirm')
    width=(W-262)//2;tap(110+width+width//2,H-44);settle(3);assert not read()['modal']
    capture('api-retry-complete')
    tap(40,32);wait(lambda:read()['navigation_open']);tap(W*2//3-70,H-30);wait(lambda:read()['view']=='settings')
    from settings_ui import locate
    b=locate(read,ctx['settings_swipe'],'models')['bounds'];button(b);wait(lambda:read()['view']=='provider')
    tap(W-90,76)
    def locate_form(key):
        for _ in range(30):
            s=read();b=s['settings']['rows'][key]['bounds']
            if b[1]>=0 and b[3]<H:return b
            ctx['settings_swipe'](max(-100,min(100,H//2-(b[1]+b[3])//2)));time.sleep(.2)
        raise AssertionError(key)
    button(locate_form('api_compatibility'));wait(lambda:read()['view']=='api_compatibility');capture('api-compatibility-settings-top')
    button(locate_form('http1'));assert not read()['settings']['rows']['http1']['checked'];capture('api-compatibility-settings-bottom')
    # This fixture is discarded/restored by the shared finally. Return to the form
    # without committing the toggle, preserving the explicit-save rule.
    tap(40,32);wait(lambda:read()['view']=='provider')
    ctx['result']['api_compatibility']={'protocol':'chat' if ctx['SERIAL'].endswith('113') else 'responses',
       'wrong_content_type_json':True,'image_input':True,'usage':True,'http1':True,'explicit_extensions':True,
       'error_without_auto_retry':True,'manual_retry':True,'requests':len(handler.requests),'physical_settings_touch':True,'custom_accept_preserved':True}
