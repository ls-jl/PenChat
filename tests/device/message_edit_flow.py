"""Real-touch message flow, called within display_config's data isolation."""
import http.server,json,re,shutil,socket,sqlite3,subprocess,sys,threading,time,types

def prepare(root,folder,data,serial,fixture_builder=None):
    sys.path.insert(0,str(root/'tests'))
    from test_message_edit import fixture,Server
    Server.mode='none';Server.requests=[]
    from local_http import server_for_device
    server,host=server_for_device(root,serial,Server)
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();(fixture_builder or fixture)(folder,server.server_port)
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
        from touch_ack import retry_unseen_tap
        for attempt in range(2):
            before=d.wait_native_input();rx,ry=d.raw(x,y)
            d.shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+d.event+' /tmp/penchat-display-tap --hold '+str(rx)+' '+str(ry)+' 750 2>/dev/null')
            try:
                d.wait(lambda:bool(d.state()['message_interactions']['actions']));return
            except AssertionError:
                after=d.state()
                if attempt or not retry_unseen_tap(before,after,allow_cancelled_down=True):raise
                probe='/tmp/penchat-release-input-state'
                kernel=json.loads(d.shell(probe+' /dev/input/'+d.event))
                if kernel.get('error') or kernel.get('btn_touch') or kernel.get('active_slots'):raise
                d.shell(probe+' /dev/input/'+d.event+' --release')
                current=d.wait_native_input()
                if not retry_unseen_tap(before,current,allow_cancelled_down=True):raise
                d.result['unobserved_message_hold_retry']=d.result.get('unobserved_message_hold_retry',0)+1
    def bounds(b):d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def menu():
        for _ in range(20):
            b=d.state()['message_interactions']['bubbles']['u2']
            if b[1]>=4 and b[3]<H-65:
                press((b[0]+b[2])//2,(b[1]+b[3])//2);d.wait(lambda:bool(d.state()['message_interactions']['actions']));return
            d.settings_swipe(100 if b[1]<4 else -100)
        raise AssertionError('User bubble never became visible')
    def action(key):bounds(d.state()['message_interactions']['actions'][key])
    def editor_action(key):
        e=d.state()['editor'];b=e['actions'][key];c=e['coordinate_space']
        d.tap(round((b[0]+b[2])*W/(2*c['width'])),round((b[1]+b[3])*H/(2*c['height'])))
    def begin():
        menu();action('edit');d.wait(lambda:d.state()['modal']);width=(W-262)//2
        d.capture('message-edit-warning');d.modal_action(1,84+14+width//2)
        d.wait(lambda:d.state()['editor']['active']);assert d.state()['editor']['length']>10
    def cancel_input():
        e=d.state()['editor']
        if 'cancel' in e['actions']:editor_action('cancel')
        else:d.side_back()
        d.wait(lambda:not d.state()['editor']['active'])
    menu();s=d.state();assert len(s['message_interactions']['actions'])==3
    d.capture('message-menu');action('copy')
    menu();action('select');d.wait(lambda:d.state()['editor']['selection_only'])
    e=d.state()['editor'];text=e['text_bounds'];c=e['coordinate_space']
    assert text[1]>max(e['actions'][key][3] for key in ('cancel','all','copy')),(text,e['actions'])
    d.double_tap(round((text[0]+12)*W/c['width']),round((text[1]+12)*H/c['height']));d.wait(lambda:d.state()['editor']['selected'])
    editor_action('copy')
    assert d.state()['editor']['clipboard_count']>=1;d.capture('message-partial-copy');cancel_input()
    begin();d.capture('message-edit-keyboard');cancel_input()
    assert not d.state()['message_interactions']['editing']
    begin();editor_action('done');d.wait(lambda:not d.state()['editor']['active'])
    assert d.state()['message_interactions']['images']==1;d.capture('message-staged')
    d.navigate_back_or_menu();assert d.state()['draft_length']==0 and not d.state()['message_interactions']['editing']
    begin()
    if d.state()['editor']['chinese']:d.key('language')
    d.key('q');editor_action('done');d.wait(lambda:not d.state()['editor']['active'])
    d.send_chat()
    d.wait(lambda:bool(Server.requests) and not d.state()['generating'] and not d.state()['compressing'] and not d.state()['message_interactions']['busy'],timeout=20)
    body=json.dumps(Server.requests[-1][1]);assert 'Later question' not in body and 'Old answer' not in body
    assert 'Old prompt hello' in body and 'q' in body
    d.capture('message-edit-sent')
    target=d.OUT/'message-edit-result.db'
    from release_lifecycle import pull_database
    pull_database(d.SERIAL,d.DATA,target,d.shell)
    with sqlite3.connect(target) as db:
        archive=db.execute("select id from chat_sessions where status='archived'").fetchone();assert archive
        assert db.execute('select count(*) from chat_messages where session_id=?',archive).fetchone()[0]==8
        assert db.execute("select count(*) from chat_messages where session_id='s'").fetchone()[0]==5
        assert db.execute('pragma foreign_key_check').fetchall()==[]
    d.result['message_long_press_edit_copy_archive']=True
