"""Conversation statistics with a real touchscreen and isolated local API fixture."""
import http.server,json,re,shutil,socket,sqlite3,subprocess,sys,threading,time,types

def prepare(root,folder,data,serial):
    sys.path.insert(0,str(root/'tests'))
    from test_session_info import fixture,Server
    route=subprocess.check_output(['adb','-s',serial,'shell','ip -4 route show default'],text=True)
    device_ip=re.search(r'\bsrc (\d+\.\d+\.\d+\.\d+)',route).group(1)
    connection=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    connection.connect((device_ip,9));host=connection.getsockname()[0];connection.close()
    Server.requests=[];Server.mode='usage'
    server=http.server.ThreadingHTTPServer((host,0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();fixture(folder,server.server_port,'chat' if serial.endswith('113') else 'responses',True)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('update providers set base_url=?',('http://'+host+':'+str(server.server_port)+'/v1',))
        db.execute('update message_assets set stored_path=?,original_path=?',(data+'/assets/original.png',)*2)
    return server,Server,[(folder/'penchat.db',data+'/penchat.db'),(folder/'assets/original.png',data+'/assets/original.png')]

def run(env,Server):
    d=types.SimpleNamespace(**env);W,H=d.W,d.H
    def bounds(b):d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def open_info():
        d.tap(128,H-34)
        for _ in range(15):
            s=d.state();b=s['attachment_actions']['session_info'];v=s['attachment_viewport']
            if b[1]>=v[1] and b[3]<=v[3]:
                d.capture('session-info-menu');bounds(b);break
            start=v[3]-10 if b[3]>v[3] else v[1]+10
            end=max(v[1]+10,start-100) if b[3]>v[3] else min(v[3]-10,start+100)
            d.drag(220,start,220,end)
        d.wait(lambda:d.state()['view']=='session_info' and bool(d.state()['session_info']) and not d.state()['session_info_busy'])
        result=d.state()['session_info'];assert 'error' not in result,result
        return result
    native=d.pid();d.tap(40,32)
    if d.state()['navigation_open']:d.tap(W-30,100)
    d.tap(W//2,H-34);d.wait(lambda:d.state()['editor']['active'])
    if d.state()['editor']['chinese']:d.key('language')
    d.key('q');bounds(d.state()['editor']['actions']['done']);d.wait(lambda:not d.state()['editor']['active'])
    before=d.state();assert before['draft_length']==1
    initial=open_info();assert initial['budget']==16384 and initial['images']==1 and initial['reported_replies']==0
    assert d.state()['session']==before['session'] and d.state()['draft_length']==1
    d.capture('session-info-unknown');d.side_back();d.wait(lambda:d.state()['view']=='chat')
    assert d.state()['draft_length']==1 and d.state()['scroll_y']==before['scroll_y']
    d.tap(W-52,H-34)
    d.wait(lambda:len(Server.requests)==1 and not d.state()['generating'] and not d.state()['compressing'],timeout=20)
    metrics=open_info();assert metrics['latest']['total_tokens']==1200 and metrics['latest']['cached_tokens']==600
    assert metrics['latest']['reasoning_tokens']==50 and metrics['reported_replies']==1 and metrics['replies']==2
    assert metrics['cache_read_tokens']/metrics['cache_input_tokens']==.6
    d.capture('session-info-current');d.settings_swipe(-120);d.capture('session-info-cache')
    for _ in range(4):d.settings_swipe(-120)
    d.capture('session-info-totals')
    for _ in range(4):d.settings_swipe(-120)
    d.capture('session-info-notes')
    d.side_back();d.wait(lambda:d.state()['view']=='chat');assert d.pid()==native and len(Server.requests)==1
    d.result['session_info_context_api_cache_reasoning_and_draft']=True
    d.result['session_info_api']='chat' if d.SERIAL.endswith('113') else 'responses'
