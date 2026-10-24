"""Real-touch message flow, called within display_config's data isolation."""
import http.server,json,re,shutil,socket,sqlite3,subprocess,sys,threading,time,types

def prepare(root,folder,data,serial):
    sys.path.insert(0,str(root/'tests'))
    from test_message_edit import fixture,Server
    Server.mode='none';Server.requests=[]
    route=subprocess.check_output(['adb','-s',serial,'shell','ip -4 route show default'],text=True)
    device_ip=re.search(r'\bsrc (\d+\.\d+\.\d+\.\d+)',route).group(1)
    connection=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    connection.connect((device_ip,9));host=connection.getsockname()[0];connection.close()
    server=http.server.ThreadingHTTPServer((host,0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();fixture(folder,server.server_port)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('update providers set base_url=?',('http://'+host+':'+str(server.server_port)+'/v1',))
        db.execute('update message_assets set original_path=?',(data+'/original.png',))
        for name in ('photo','pending'):db.execute('update message_assets set stored_path=? where id=?',(data+'/assets/'+name+'.png',name))
    files=[(folder/'penchat.db',data+'/penchat.db'),(folder/'original.png',data+'/original.png')]
    files += [(folder/'assets'/(name+'.png'),data+'/assets/'+name+'.png') for name in ('photo','pending')]
    return server,Server,files

def run(env,Server):
    d=types.SimpleNamespace(**env);W,H=d.W,d.H
    def press(x,y):
        rx,ry=d.raw(x,y)
        d.shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+d.event+' /tmp/penchat-display-tap --hold '+str(rx)+' '+str(ry)+' 650 2>/dev/null')
        time.sleep(.1)
    def bounds(b):d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def menu():
        for _ in range(20):
            b=d.state()['message_interactions']['bubbles']['u2']
            if b[1]>=4 and b[3]<H-65:
                press((b[0]+b[2])//2,(b[1]+b[3])//2);d.wait(lambda:bool(d.state()['message_interactions']['actions']));return
            d.settings_swipe(100 if b[1]<4 else -100)
        raise AssertionError('User bubble never became visible')
    def action(key):bounds(d.state()['message_interactions']['actions'][key])
    def begin():
        menu();action('edit');d.wait(lambda:d.state()['modal']);width=(W-262)//2
        d.capture('message-edit-warning');d.tap(84+14+width//2,H-44)
        d.wait(lambda:d.state()['editor']['active']);assert d.state()['editor']['length']>10
    def cancel_input():
        split=W-max(352,W*2//5-8);tw=W-split-16
        d.tap(split+8+(tw-276)//2,H-30);d.wait(lambda:not d.state()['editor']['active'])
    menu();s=d.state();assert len(s['message_interactions']['actions'])==3
    d.capture('message-menu');action('copy')
    menu();action('select');d.wait(lambda:d.state()['editor']['selection_only'])
    d.tap(115,75);d.tap(115,75);d.wait(lambda:d.state()['editor']['selected'])
    d.tap(W-62,28);assert d.state()['editor']['clipboard_count']>=1;d.capture('message-partial-copy');d.tap(40,30)
    begin();d.capture('message-edit-keyboard');cancel_input()
    assert not d.state()['message_interactions']['editing']
    begin();d.tap(W-80,H-30);d.wait(lambda:not d.state()['editor']['active'])
    assert d.state()['message_interactions']['images']==1;d.capture('message-staged')
    d.tap(40,30);assert d.state()['draft_length']==0 and not d.state()['message_interactions']['editing']
    begin()
    if d.state()['editor']['chinese']:d.key('language')
    d.key('q');d.tap(W-80,H-30);d.wait(lambda:not d.state()['editor']['active'])
    d.tap(W-52,H-34)
    d.wait(lambda:bool(Server.requests) and not d.state()['generating'] and not d.state()['compressing'] and not d.state()['message_interactions']['busy'],timeout=20)
    body=json.dumps(Server.requests[-1][1]);assert 'Later question' not in body and 'Old answer' not in body
    assert 'Old prompt hello' in body and 'q' in body
    d.capture('message-edit-sent')
    target=d.OUT/'message-edit-result.db'
    subprocess.run(['adb','-s',d.SERIAL,'pull',d.DATA+'/penchat.db',str(target)],check=True,capture_output=True)
    with sqlite3.connect(target) as db:
        archive=db.execute("select id from chat_sessions where status='archived'").fetchone();assert archive
        assert db.execute('select count(*) from chat_messages where session_id=?',archive).fetchone()[0]==8
        assert db.execute("select count(*) from chat_messages where session_id='s'").fetchone()[0]==5
        assert db.execute('pragma foreign_key_check').fetchall()==[]
    d.result['message_long_press_edit_copy_archive']=True
