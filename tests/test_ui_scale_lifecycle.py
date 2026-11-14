#!/usr/bin/env python3
"""Deferred scale acknowledgement, reading anchors, live replies and cache reuse."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_chat_style import seed
from ui_scale_ui import open_settings,back,choose,save,tap

t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
for w,h in [(960,266),(640,260),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-scale-anchor-') as folder:
        folder=pathlib.Path(folder)
        seed(folder,'\n\n'.join(f'第 {i} 段阅读锚点。中文和 English words 保持在同一阅读段落。'*4 for i in range(60)))
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h})
        try:
            # Move up from the actual bottom without entering pull-to-create.
            for _ in range(max(3,h//90+3) if h>w else 3):
                start=h//3;end=min(h-65,start+110)
                for i in range(7):a.send('touch',x=w//2,y=start+(end-start)*i//6,pressed=True);time.sleep(.025)
                time.sleep(.2);a.send('touch',x=w//2,y=end,pressed=False);time.sleep(.2)
            assert not a.diag()['following_latest']
            initial=None
            for percent in [75,125,150,100]:
                open_settings(a);anchor=a.diag()['ui_scale']['anchor']
                assert anchor['message'] and anchor['region'] and anchor['offset']>0,anchor
                if initial:
                    assert anchor['message']==initial['message'] and abs(anchor['offset']-initial['offset'])<160,(initial,anchor)
                else:initial=anchor
                a.setting('ui_scale');choose(a,percent);save(a,percent);back(a)
                d=a.diag();assert not d['following_latest'] and not d['ui_scale']['anchor']['pending']
                assert d['scroll_y']>0 and d['scroll_bottom']>0,(w,percent,anchor,d['scroll_y'],d['scroll_bottom'],d['ui_scale']['anchor'])
            print('SCALE_TEXT_ANCHOR',w,h,flush=True)
        finally:a.close()

# Hold the worker's write using a real SQLite lock, then hide the app before its
# acknowledgement. No production test command or synthetic save event is needed.
with tempfile.TemporaryDirectory(prefix='penchat-scale-focus-') as folder:
    folder=pathlib.Path(folder);a=t.Native(BINARY,folder,seed_model=False)
    try:
        open_settings(a);a.setting('ui_scale');choose(a,125)
        # Reveal Save before locking (page navigation is independent of storage).
        for _ in range(20):
            if a.diag()['scroll_y']<=0:break
            a.settings_swipe(120)
        button=a.diag()['page']['actions']['保存']
        lock=sqlite3.connect(folder/'penchat.db');lock.execute('begin immediate')
        tap(a,button,wait_motion=False);tap(a,button,wait_motion=False)
        # Diagnostic includes DB queries; do not put it ahead of releasing the
        # real SQLite lock, otherwise the test itself would wait on the worker.
        a.send('window.focus',sequence=1,focused=False);time.sleep(.1)
        lock.commit();lock.close()
        t.wait_for(lambda:a.diag()['ui_scale']['ready'])
        d=a.diag()['ui_scale'];assert d['applied']==100 and d['saved']==125
        for seq in range(2,8):
            a.send('window.focus',sequence=seq,focused=True);time.sleep(.05)
            if a.diag()['ui_scale']['applied']==125:break
        assert a.diag()['view']=='settings' and a.diag()['ui_scale']['applied']==125
        assert a.diag()['ui_scale']['changes']==1
        print('SCALE_SAVE_DEFERRED_UNTIL_FOREGROUND',flush=True)
    finally:a.close()

class Stream(http.server.BaseHTTPRequestHandler):
    release=threading.Event()
    def log_message(self,*args):pass
    def do_POST(self):
        request=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
        def emit(v):self.wfile.write(('data: '+json.dumps(v)+'\n\n').encode());self.wfile.flush()
        emit({'type':'response.output_text.delta','delta':'第一部分。\n\n'+('中文 **正文** $x^2$。\n\n'*30)})
        self.release.wait(25)
        emit({'type':'response.output_text.delta','delta':'最后一部分，完整保留。'})
        emit({'type':'response.completed'})
        self.close_connection=True
with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-scale-stream-') as folder:
    threading.Thread(target=server.serve_forever,daemon=True).start();t.port=server.server_port
    a=t.Native(BINARY,folder)
    try:
        a.edit('开始流式测试');a.tap(908,232);t.wait_for(lambda:a.diag()['generating'])
        a.edit('保留后续草稿')
        open_settings(a);a.setting('ui_scale');choose(a,125);save(a,125)
        assert a.diag()['generating']
        back(a);Stream.release.set();t.wait_for(lambda:not a.diag()['generating'],15)
        assert a.diag()['draft_length']==len('保留后续草稿')
        rows=a.read_messages();assert len(rows)==2 and rows[-1][2].endswith('最后一部分，完整保留。')
        faces=None
        for _ in range(2):
            for percent in [75,150,100]:open_settings(a);a.setting('ui_scale');choose(a,percent);save(a,percent);back(a)
            current=a.diag()['font_cache']['faces']
            if faces is not None:assert current==faces
            faces=current
        assert a.diag()['font_cache']['bytes']<=4*1024*1024
        print('SCALE_STREAM_DRAFT_AND_REPEATED_SWITCHES',flush=True)
    finally:Stream.release.set();a.close();server.shutdown()
