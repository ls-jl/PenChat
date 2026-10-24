#!/usr/bin/env python3
"""Actual LVGL message menu, read-only selection and two-phase branch editing."""
import http.server,json,os,pathlib,signal,sqlite3,struct,tempfile,threading,time,zlib
import test_lvgl_app as t
from test_reasoning_ui import seed,Server
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/message-edit';OUT.mkdir(parents=True,exist_ok=True)

def png(path):
    def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',96,64,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([65,140,205])*96)*64))+chunk(b'IEND',b''))

def fixture(folder,port):
    seed(folder,port);(folder/'assets').mkdir();png(folder/'original.png')
    for key in ('photo','pending'):(folder/'assets'/(key+'.png')).write_bytes((folder/'original.png').read_bytes())
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('s','编辑测试','active',1,1,1)")
        for key,value in [('active_session_id','s'),('auto_title_enabled',False)]:db.execute('insert into app_settings values(?,?,1)',(key,json.dumps(value)))
        for seq,(id,role,text) in enumerate([('u1','user','Prefix question'),('a1','assistant','Prefix answer'),('photo','user',''),('u2','user','Old prompt hello 世界 👩‍💻'),('a2','assistant','Old answer'),('u3','user','Later question'),('a3','assistant','Later answer'),('pending','user','')],1):
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values(?,'s',?,?,'complete',?,1,1,?)",(id,seq,role,'image' if not text else 'text',text))
            if not text:
                db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,created_at) values(?,'s',?,'image','album',?,?,1)",(id,id,str(folder/'original.png'),str(folder/'assets'/(id+'.png'))))
                db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values(?,'s',?,0,'image',?)",(id,id,id))
            else:db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,text) values(?,'s',?,0,'text',?)",(id,id,text))

def shot(a,name):
    p=a.directory/'lvgl-diagnostic.png'
    if p.exists():p.unlink()
    a.process.send_signal(signal.SIGUSR1);t.wait_for(p.exists);(OUT/(name+'.png')).write_bytes(p.read_bytes())

def button(a,b):
    x1,y1,x2,y2=b;a.tap((x1+x2)//2,(y1+y2)//2)

def menu(a,id='u2'):
    for _ in range(25):
        s=a.diag();r=s['message_interactions']['bubbles'].get(id)
        if r and r[1]>=5 and r[3]<s['height']-65:
            a.long_press((r[0]+r[2])//2,(r[1]+r[3])//2);t.wait_for(lambda:bool(a.diag()['message_interactions']['actions']));return
        if not r:raise AssertionError('Missing user message '+id)
        a.settings_swipe(100 if r[1]<5 else -100);time.sleep(.25)
    raise AssertionError(('Cannot reveal message',id,a.diag()))

def action(a,key):button(a,a.diag()['message_interactions']['actions'][key])

def begin(a,archive):
    menu(a);action(a,'edit');t.wait_for(lambda:a.diag()['modal'])
    w=a.diag()['width'];width=(w-262)//2
    a.tap(84+(14 if archive else 26+width)+width//2,a.diag()['height']-44)
    request=a.receive('keyboard.open');assert request['text']=='Old prompt hello 世界 👩‍💻'
    return request

def rows(folder):
    with sqlite3.connect(folder/'penchat.db') as db:return db.execute("select id,role,output_text from chat_messages where session_id='s' order by seq").fetchall()

def run(w,h):
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-message-ui-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);fixture(p,server.server_port)
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        try:
            original=rows(p)
            (p/'DCIM').mkdir();png(p/'DCIM/extra.png')
            menu(a);actions=a.diag()['message_interactions']['actions'];assert len(actions)==3
            assert len({b[1] for b in actions.values()})==3 and all(b[3]-b[1]+1>=44 for b in actions.values())
            shot(a,f'{w}-vertical-menu');action(a,'copy');assert rows(p)==original
            menu(a);action(a,'select');t.wait_for(lambda:a.diag()['editor']['selection_only'])
            assert not a.diag()['editor']['keys'] and not a.diag()['editor']['ocr_active']
            a.tap(115,75);a.tap(115,75);t.wait_for(lambda:a.diag()['editor']['selected'])
            selected=a.diag()['editor'];assert selected['selection_end']-selected['selection_start']<selected['length']
            a.tap(w-62,28);shot(a,f'{w}-partial-selection');assert a.diag()['editor']['clipboard_count']>=1
            a.tap(40,30);assert rows(p)==original and not a.diag()['editor']['active']
            # Initial keyboard cancel is a full branch cancel; no archive yet.
            request=begin(a,True)
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='discard')
            t.wait_for(lambda:not a.diag()['editor']['active']);assert not a.diag()['message_interactions']['editing'] and rows(p)==original
            # Done only stages. X restores the old pending image and clears text.
            request=begin(a,True);a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='Changed question')
            t.wait_for(lambda:not a.diag()['editor']['active']);s=a.diag()
            assert s['message_interactions']['editing'] and s['message_interactions']['images']==1 and s['navigation_button']=='close'
            assert rows(p)==original and 'u3' not in s['message_interactions']['bubbles']
            shot(a,f'{w}-staged-branch')
            # New album images stay private/in-memory until Send, even with auto-send enabled.
            a.tap(128,h-34);a.tap(220,82);t.wait_for(lambda:a.diag()['view']=='album')
            a.tap(200,98);t.wait_for(lambda:a.diag()['view']=='chat' and not a.diag()['modal'])
            assert a.diag()['message_interactions']['images']==2 and rows(p)==original
            time.sleep(.3)
            photo=next(v for v in a.diag()['chat_images'] if 20<=v['y']+v['height']//2<h-58)
            a.long_press(photo['x']+photo['width']//2,photo['y']+photo['height']//2);t.wait_for(lambda:a.diag()['modal'])
            a.tap(w-190,h-44);t.wait_for(lambda:a.diag()['message_interactions']['images']==1)
            assert rows(p)==original
            a.tap(40,30)
            assert not list((p/'.message-edit').glob('image_*'))
            assert rows(p)==original and a.diag()['draft_length']==0 and not a.diag()['message_interactions']['editing']
            # Send archives the complete original, rewrites only suffix, and uses new input.
            request=begin(a,True);a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='Edited and sent')
            t.wait_for(lambda:not a.diag()['editor']['active']);a.tap(w-52,h-34)
            t.wait_for(lambda:bool(Server.requests) and not a.diag()['generating'] and not a.diag()['compressing'] and not a.diag()['message_interactions']['busy'])
            with sqlite3.connect(p/'penchat.db') as db:
                archived=db.execute("select id from chat_sessions where status='archived'").fetchall();assert len(archived)==1
                assert db.execute('select count(*) from chat_messages where session_id=?',archived[0]).fetchone()[0]==len(original)
                current=rows(p);assert current[0][0]=='u1' and current[1][0]=='a1'
                assert any(r[2]=='Edited and sent' for r in current) and not any(r[2] in ('Old answer','Later question','Later answer') for r in current)
            body=json.dumps(Server.requests[-1][1]);assert 'Edited and sent' in body and 'Later question' not in body and 'Old prompt' not in body
            assert (p/'original.png').exists() and (p/'assets/photo.png').exists()
            shot(a,f'{w}-sent-archived')
            # Default edit policy persists, and no warning appears on next edit.
            a.open_settings();a.setting('message_edit');shot(a,f'{w}-default-policy');a.tap(84+16+(w-162)//3+12+(w-162)//6,h-98)
            with sqlite3.connect(p/'penchat.db') as db:assert json.loads(db.execute("select value_json from app_settings where key='message_edit_default'").fetchone()[0])=='archive'
            a.to_chat()
            edited_id=next(id for id,role,text in rows(p) if text=='Edited and sent')
            menu(a,edited_id);action(a,'edit');request=a.receive('keyboard.open')
            assert request['text']=='Edited and sent' # saved policy bypasses the warning
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='')
            t.wait_for(lambda:not a.diag()['editor']['active']);assert not a.diag()['message_interactions']['editing']
        finally:a.close();server.shutdown()
def generation_guard():
    gate=threading.Event()
    class Hold(http.server.BaseHTTPRequestHandler):
        received=False
        def log_message(self,*args):pass
        def do_POST(self):
            self.rfile.read(int(self.headers['Content-Length']));Hold.received=True
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.end_headers()
            gate.wait(15)
            try:self.wfile.write(b'data: {"type":"response.output_text.delta","delta":"done"}\n\ndata: {"type":"response.completed"}\n\n');self.wfile.flush()
            except BrokenPipeError:pass
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Hold) as server,tempfile.TemporaryDirectory(prefix='penchat-edit-generating-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        a=t.Native(BINARY,p)
        try:
            a.edit('Generation must block editing');a.tap(908,232);t.wait_for(lambda:Hold.received and a.diag()['generating'])
            with sqlite3.connect(p/'penchat.db') as db:id=db.execute("select id from chat_messages where role='user'").fetchone()[0]
            menu(a,id);action(a,'edit')
            assert a.diag()['generating'] and a.diag()['message_interactions']['actions'] and not a.diag()['editor']['active'] and not a.diag()['message_interactions']['editing']
            shot(a,'generation-edit-disabled');gate.set();t.wait_for(lambda:not a.diag()['generating'])
        finally:gate.set();a.close();server.shutdown()
if __name__=='__main__':
    for size in ((960,266),(936,280)):run(*size)
    generation_guard()
    print('Message interactions passed: both screen sizes, vertical menu, partial copy, staged image add/delete/cancel, archive-at-send, prefix/context preservation, default-policy bypass and generation guard.')
