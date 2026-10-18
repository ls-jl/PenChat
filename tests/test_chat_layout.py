#!/usr/bin/env python3
import http.server,json,os,pathlib,sqlite3,struct,tempfile,threading,time,zlib
from test_lvgl_app import Native,ROOT,wait_for
import test_lvgl_app as transport
from test_chat_style import seed,snapshot
from test_reasoning_ui import Server,seed as seed_model
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
transport.port=1
with tempfile.TemporaryDirectory(prefix='penchat-thought-collapse-') as directory:
    seed(directory,'简短答复。')
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
        db.execute('update chat_messages set reasoning_text=?',('较长的公开思考内容。'*80,))
    app=Native(BINARY,pathlib.Path(directory))
    try:
        for _ in range(12):
            d=app.diag();app.tap(d['reasoning_header_x'],d['reasoning_header_y'])
            assert app.diag()['reasoning_expanded_count']==1
            d=app.diag();app.tap(d['reasoning_header_x'],d['reasoning_header_y'])
            d=app.diag();assert d['reasoning_expanded_count']==0 and d['scroll_y']==0 and d['following_latest']
        app.edit('收起后仍可正常输入')
        app.open_settings();app.tap(730,192);assert app.diag()['modal'];snapshot(app,'thought-display-options')
        app.tap(800,96);assert not app.diag()['modal']
        with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:assert json.loads(db.execute("select value_json from app_settings where key='reasoning_display'").fetchone()[0])=='always'
        app.to_chat();assert app.diag()['reasoning_expanded_count']==1
    finally:app.close()
    app=Native(BINARY,pathlib.Path(directory))
    try:assert app.diag()['reasoning_expanded_count']==1
    finally:app.close()
with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server:
    transport.port=server.server_port;Server.mode='delayed';threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        for mode in ['collapsed','during','always']:
            with tempfile.TemporaryDirectory(prefix='penchat-auto-thought-') as directory:
                seed_model(directory,server.server_port)
                with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:db.execute('insert into app_settings values(?,?,1)',('reasoning_display',json.dumps(mode)))
                # Return text during the delayed thinking phase.
                Server.mode='summary-delayed'
                app=Native(BINARY,pathlib.Path(directory))
                try:
                    app.edit('自动展开测试');app.tap(908,232)
                    if mode!='collapsed':wait_for(lambda:app.diag()['reasoning_expanded_count']==1)
                    wait_for(lambda:not app.diag()['generating'])
                    assert app.diag()['reasoning_expanded_count']==(1 if mode=='always' else 0)
                finally:app.close()
    finally:server.shutdown();server.server_close()

def png(path,w,h):
    def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
    raw=(b'\0'+bytes([80,150,210])*w)*h
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
with tempfile.TemporaryDirectory(prefix='penchat-photo-layout-') as directory:
    seed(directory,'')
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
        db.execute('delete from chat_messages')
        for i,(w,h) in enumerate([(40,30),(30,30),(50,30),(1000,100),(100,1000),(20,20)]):
            path=pathlib.Path(directory)/f'{i}.png';png(path,w,h)
            mid='m'+str(i)
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'fixture',?,'user','complete','image','',1,1)",(mid,i))
            db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,stored_path,created_at) values(?,'fixture',?,'image','album',?,1)",(mid,mid,str(path)))
            db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values(?,'fixture',?,0,'image',?)",(mid,mid,mid))
    app=Native(BINARY,pathlib.Path(directory))
    try:
        wait_for(lambda:len(app.diag()['chat_images'])==6)
        time.sleep(.5)
        photos=sorted(app.diag()['chat_images'],key=lambda p:(p['y'],p['x']))
        assert all(p['radius']==12 and 0<p['height']<=140 and 0<p['width']<=848 for p in photos)
        assert photos[0]['y']==photos[1]['y']==photos[2]['y']
        assert photos[3]['y']>photos[2]['y'] and photos[3]['width']==848
        assert photos[4]['y']==photos[5]['y']>photos[3]['y']
        assert photos[4]['width']==14 and photos[5]['width']==140
        snapshot(app,'rounded-photo-layout')
    finally:app.close()
print('Short thought collapse, display settings/persistence/streaming policy and responsive rounded image wrapping passed.')
