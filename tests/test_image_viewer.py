#!/usr/bin/env python3
"""Actual-process image viewing, original scan detail, gestures and chat preservation."""
import http.server,json,os,pathlib,signal,sqlite3,struct,tempfile,threading,time,zlib
import test_lvgl_app as t
from test_reasoning_ui import seed
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/image-viewer';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def png(path,w,h):
    def chunk(k,v):return struct.pack('>I',len(v))+k+v+struct.pack('>I',zlib.crc32(k+v))
    rows=[]
    for y in range(h):rows.append(b'\0'+b''.join(bytes([220 if (x//32+y//32)%2 else 70,x*200//w,y*200//h]) for x in range(w)))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))

def fixture(p,kind='photo',port=1):
    seed(p,port);(p/'assets').mkdir()
    original=p/'assets/original.png';preview=p/'assets/preview.png'
    w,h=(12000,32) if kind=='scan' else (1024,768)
    if kind!='missing':png(original,w,h)
    png(preview,100,100)
    with sqlite3.connect(p/'penchat.db') as db:
        db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('fixture','Viewer fixture','active',1,1,1)")
        db.execute("insert into app_settings values('active_session_id','\"fixture\"',1)")
        db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('photo','fixture',1,'user','complete','image','',1,1)")
        db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,display_src,width,height,created_at) values('asset','fixture','photo','image',?,?,?,?,?,?,1)",(kind,str(original),str(original),str(preview) if kind!='photo' else None,w,h))
        db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('part','fixture','photo',0,'image','asset')")
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('reply','fixture',2,'assistant','complete','text','已收到图片。',1,1)")

def shot(a,name):
    file=a.directory/'lvgl-diagnostic.png'
    if file.exists():file.unlink()
    a.process.send_signal(signal.SIGUSR1);t.wait_for(file.exists)
    (OUT/(name+'.png')).write_bytes(file.read_bytes())

def open_photo(a):
    def visible():
        return next((v for v in a.diag()['chat_images'] if v['y']>=0 and v['y']+v['height']<=a.diag()['height']),None)
    photo=visible()
    if not photo:
        for _ in range(5):
            a.settings_swipe(120);time.sleep(.3);photo=visible()
            if photo:break
    assert photo
    t.wait_for(lambda:not a.diag()['image_viewer']['open'])
    before=a.diag()
    a.tap(photo['x']+photo['width']//2,photo['y']+photo['height']//2)
    t.wait_for(lambda:a.diag()['image_viewer']['open'])
    return before

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-viewer-') as directory:
        p=pathlib.Path(directory);fixture(p)
        a=t.Native(BINARY,p,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            a.tap(w//2,h-34);t.wait_for(lambda:a.diag()['editor']['active'])
            keys={k['key']:k for k in a.diag()['editor']['keys']}
            k=keys['language'];a.tap(k['x'],k['y'])
            for c in 'draft':k=next(k for k in a.diag()['editor']['keys'] if k['key']==c);a.tap(k['x'],k['y'])
            a.tap(w-80,h-30);t.wait_for(lambda:not a.diag()['editor']['active'])
            before=a.diag();assert before['draft_length']==5
            clicked=open_photo(a);t.wait_for(lambda:a.diag()['image_viewer']['loaded'])
            assert a.diag()['image_viewer']['frame']==[1024,768]
            shot(a,f'{w}-fit')
            a.tap(w-40,114);a.tap(w-40,114);assert a.diag()['image_viewer']['zoom']==4
            pan=a.diag()['image_viewer']['pan']
            a.send('touch',x=400,y=130,pressed=True)
            for x,y in [(350,100),(280,80),(160,60)]:a.send('touch',x=x,y=y,pressed=True);time.sleep(.025)
            a.send('touch',x=160,y=60,pressed=False)
            assert a.diag()['image_viewer']['pan']!=pan
            assert not a.diag()['pull_active'] and not a.diag()['editor']['active']
            shot(a,f'{w}-zoom-pan')
            a.tap(w-70,166);assert a.diag()['image_viewer']['zoom']==1
            a.tap(400,130);a.tap(400,130);assert a.diag()['image_viewer']['zoom']==2
            a.send('touch',x=400,y=130,pressed=True);a.send('touch',x=200,y=80,pressed=True)
            a.send('touch',x=200,y=80,pressed=False,cancelled=True)
            a.send('visibility',visible=False);a.send('visibility',visible=True)
            assert a.diag()['image_viewer']['open'] and a.diag()['image_viewer']['zoom']==2
            a.tap(w-38,30);t.wait_for(lambda:not a.diag()['image_viewer']['open'])
            after=a.diag();assert after['session']==before['session'] and after['draft_length']==5
            assert after['scroll_y']==clicked['scroll_y'] and len(a.read_messages())==2
            for _ in range(10):
                open_photo(a);a.tap(w-38,30);t.wait_for(lambda:not a.diag()['image_viewer']['open'])
        finally:a.close()

class Stream(http.server.BaseHTTPRequestHandler):
    release=threading.Event()
    def log_message(self,*args):pass
    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
        try:
            self.wfile.write(b'data: {"type":"response.output_text.delta","delta":"stream"}\n\n');self.wfile.flush()
            self.release.wait(10)
            self.wfile.write(b'data: {"type":"response.completed"}\n\n');self.wfile.flush()
        except (BrokenPipeError,ConnectionResetError):pass
        self.close_connection=True

def stream_test():
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-viewer-stream-') as directory:
        threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(directory);fixture(p,port=server.server_port)
        a=t.Native(BINARY,p)
        try:
            a.edit('继续回复');a.tap(908,232);t.wait_for(lambda:a.diag()['generating'])
            open_photo(a);t.wait_for(lambda:a.diag()['image_viewer']['loaded'])
            Stream.release.set();t.wait_for(lambda:not a.diag()['generating'])
            assert a.diag()['image_viewer']['open'] and a.diag()['image_viewer']['loaded']
            a.tap(922,30);t.wait_for(lambda:not a.diag()['image_viewer']['open'])
            assert a.read_messages()[-1][1]=='complete'
        finally:Stream.release.set();a.close();server.shutdown()

if __name__=='__main__':
    for w,h in [(960,266),(936,280)]:run(w,h)
    for kind in ('scan','missing'):
        with tempfile.TemporaryDirectory(prefix='penchat-viewer-source-') as directory:
            p=pathlib.Path(directory);fixture(p,kind);a=t.Native(BINARY,p)
            try:
                open_photo(a)
                if kind=='scan':
                    t.wait_for(lambda:a.diag()['image_viewer']['loaded'])
                    assert a.diag()['image_viewer']['frame']==[12000,32]
                    for _ in range(4):a.tap(920,114)
                    assert a.diag()['image_viewer']['zoom']==16
                else:t.wait_for(lambda:a.diag()['image_viewer']['failed'])
                shot(a,kind);a.tap(922,30);t.wait_for(lambda:not a.diag()['image_viewer']['open'])
            finally:a.close()
    stream_test()
    print('Image viewer: original sources, full-screen fit, zoom/pan, double tap, cancellation, lifecycle, draft/scroll preservation, repeated close and streaming rebuilds passed.')
