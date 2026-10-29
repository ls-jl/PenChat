#!/usr/bin/env python3
"""Gated live output: layout-free action hiding, user-owned scrolling and follow."""
import http.server,json,os,pathlib,queue,signal,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_reasoning_ui import seed

BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/stream-reading-0.2.15';OUT.mkdir(parents=True,exist_ok=True)

class Stream(http.server.BaseHTTPRequestHandler):
    commands=queue.Queue()
    def log_message(self,*args):pass
    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
        chat=self.path.endswith('/chat/completions')
        def emit(obj):self.wfile.write(('data: '+json.dumps(obj,ensure_ascii=False)+'\n\n').encode());self.wfile.flush()
        try:
            while True:
                kind,text=self.commands.get(timeout=30)
                if kind=='cut':return
                if kind=='done':
                    if chat:
                        emit({'choices':[{'delta':{},'finish_reason':'stop'}]});self.wfile.write(b'data: [DONE]\n\n');self.wfile.flush()
                    else:emit({'type':'response.completed'})
                    return
                if chat:emit({'choices':[{'delta':{'reasoning_content' if kind=='thought' else 'content':text}}]})
                else:emit({'type':'response.reasoning_summary_text.delta' if kind=='thought' else 'response.output_text.delta','output_index':0 if kind=='thought' else 1,'summary_index':0,'delta':text})
        except (BrokenPipeError,ConnectionResetError,queue.Empty):pass

def shot(a,name):
    image=a.directory/'lvgl-diagnostic.png'
    if image.exists():image.unlink()
    a.process.send_signal(signal.SIGUSR1);t.wait_for(image.exists);(OUT/(name+'.png')).write_bytes(image.read_bytes())

def run(w,h,api,thinking=False):
    Stream.commands=queue.Queue()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-live-reader-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute('update providers set metadata_json=?',(json.dumps({'api':api}),))
            for key,value in [('auto_title_enabled',False),('auto_context_compression',False),('reasoning_display','always')]:
                db.execute('insert or replace into app_settings values(?,?,1)',(key,json.dumps(value)))
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0});length=0
        def emit(text,thought=False):
            nonlocal length
            Stream.commands.put(('thought' if thought else 'text',text))
            if not thought:
                length+=len(text.encode());t.wait_for(lambda:a.diag()['output_length']==length)
        def settled():t.wait_for(lambda:not a.diag()['reader_interacting'] and not a.diag()['chat_render_pending'])
        def bottom():
            s=a.diag();assert s['to_bottom_visible'],s
            a.tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
            t.wait_for(lambda:a.diag()['following_latest'] and a.diag()['at_latest_bottom'] and not a.diag()['to_bottom_visible'])
        def swipe(points,release=True):
            for y in points:a.send('touch',x=w//2,y=y,pressed=True);time.sleep(.035)
            if release:a.send('touch',x=w//2,y=points[-1],pressed=False)
        try:
            a.edit('reader test');a.tap(w-52,h-34);t.wait_for(lambda:a.diag()['receiving_reply'])
            rows=a.diag()['reply_actions']['rows'];assert rows
            id=next(iter(rows));assert not rows[id]['visible'] and not rows[id]['copy_enabled']
            t.wait_for(lambda:a.diag()['generation_hint_present'])
            shot(a,f'{w}-{api}-waiting')
            if thinking:
                emit('公开思考\n\n继续思考。',True)
                settled();assert a.diag()['generation_hint_present']
            emit('short');settled();s=a.diag();assert s['at_latest_bottom'] and not s['to_bottom_visible']
            assert not s['generation_hint_present']
            if thinking:
                emit('补充公开思考',True);time.sleep(.15);settled()
                assert not a.diag()['generation_hint_present']
            shortHeight=s['reply_actions']['rows'][id]['message'][3]-s['reply_actions']['rows'][id]['message'][1]
            shot(a,f'{w}-{api}-short-hidden')
            emit('\n\n'+'\n\n'.join(f'段落 {i}: reading while output continues.' for i in range(25)))
            settled();s=a.diag();assert s['following_latest'] and s['scroll_bottom']<=10 and not s['to_bottom_visible']
            shot(a,f'{w}-{api}-following')
            # Keep the finger down as deltas arrive; no reflow may cancel it.
            swipe([45,65,90,120,150],False)
            t.wait_for(lambda:a.diag()['reader_interacting'] and not a.diag()['following_latest'])
            before=a.diag();maximum=before['scroll_y']+before['scroll_bottom']
            emit('\n\nnew content during the drag\n\n'*8)
            time.sleep(.18);s=a.diag()
            assert s['reader_interacting'] and s['chat_render_pending'] and not s['following_latest']
            assert not s['generation_hint_present']
            assert s['scroll_y']+s['scroll_bottom']==maximum
            swipe([180,210],False);assert a.diag()['scroll_y']<before['scroll_y']
            a.send('touch',x=w//2,y=210,pressed=False);settled()
            s=a.diag();assert not s['following_latest'] and s['to_bottom_visible']
            scroll=s['scroll_y'];emit('\n\nmore after release\n\n'*4);settled()
            assert a.diag()['scroll_y']==scroll and a.diag()['to_bottom_visible']
            shot(a,f'{w}-{api}-reading')
            # Manually reaching the latest bottom also resumes follow, only
            # after the touch/inertia ends. Do not issue another pull there.
            for _ in range(20):
                if a.diag()['at_latest_bottom']:break
                a.settings_swipe(-150);time.sleep(.15)
            settled();assert a.diag()['following_latest'] and not a.diag()['to_bottom_visible']
            emit('\n\nmanual follow resumed');settled();assert a.diag()['at_latest_bottom']
            swipe([40,70,110,150,195]);settled();assert a.diag()['to_bottom_visible']
            bottom();emit('\n\nfollow resumed\n\n'*6);settled()
            s=a.diag();assert s['following_latest'] and s['scroll_bottom']<=10 and not s['to_bottom_visible']
            shot(a,f'{w}-{api}-resumed')
            # Completion during a held scroll must not delete the pressed UI.
            swipe([40,70,100,135,170],False);t.wait_for(lambda:a.diag()['reader_interacting'])
            Stream.commands.put(('done',''));t.wait_for(lambda:not a.diag()['generating'])
            assert a.diag()['reader_interacting'] and a.diag()['chat_render_pending']
            a.send('touch',x=w//2,y=170,pressed=False);settled()
            s=a.diag();assert not s['following_latest'] and s['to_bottom_visible'] and s['reply_actions']['rows'][id]['visible']
            scroll=s['scroll_y'];time.sleep(.2);assert a.diag()['scroll_y']==scroll
            bottom();shot(a,f'{w}-{api}-complete-actions')
        finally:Stream.commands.put(('cut',''));a.close()
    print(f'Stream reader {api} {w}x{h}: hidden live bar, drag/inertia, coalescing, continued follow and completion during drag passed.',flush=True)


def waiting_terminal(mode):
    Stream.commands=queue.Queue()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-waiting-terminal-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        a=t.Native(BINARY,p)
        try:
            a.edit('waiting');a.tap(908,232);t.wait_for(lambda:a.diag()['generation_hint_present'])
            if mode=='stop':a.tap(908,232)
            else:Stream.commands.put((mode,''))
            t.wait_for(lambda:not a.diag()['generating'] and not a.diag()['chat_render_pending'])
            assert not a.diag()['generation_hint_present']
            with sqlite3.connect(p/'penchat.db') as db:
                assert not db.execute("select 1 from chat_messages where output_text like '%正在生成%' ").fetchall()
            shot(a,'waiting-terminal-'+mode)
        finally:Stream.commands.put(('cut',''));a.close()
    print(f'Waiting {mode}: UI-only placeholder removed without saving fake output.',flush=True)

def short_and_terminal(mode):
    Stream.commands=queue.Queue()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-terminal-reader-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        a=t.Native(BINARY,p)
        try:
            a.edit('short');a.tap(908,232);t.wait_for(lambda:a.diag()['receiving_reply'])
            Stream.commands.put(('text','SHORT'));t.wait_for(lambda:a.diag()['output_length']==5 and not a.diag()['chat_render_pending'])
            s=a.diag();id=next(iter(s['reply_actions']['rows']));bar=s['reply_actions']['rows'][id]
            assert not bar['visible'] and not s['to_bottom_visible']
            height=bar['message'][3]-bar['message'][1]
            if mode=='stop':a.tap(908,232)
            else:Stream.commands.put((mode,''))
            t.wait_for(lambda:not a.diag()['generating'] and not a.diag()['chat_render_pending'])
            s=a.diag();bar=s['reply_actions']['rows'][id]
            assert bar['visible'] and not s['to_bottom_visible'] and s['at_latest_bottom']
            assert not s['generation_hint_present']
            assert bar['message'][3]-bar['message'][1]>=height+48
            expected='complete' if mode=='done' else 'cancelled' if mode=='stop' else 'error'
            assert [r for r in a.read_messages() if r[0]=='assistant'][-1][1]==expected
            shot(a,'short-terminal-'+mode)
        finally:Stream.commands.put(('cut',''));a.close()
    print(f'Short stream {mode}: zero hidden row space and restored terminal controls passed.',flush=True)

def older_copy_and_horizontal():
    Stream.commands=queue.Queue()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-older-live-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('s','Older replies','active',1,1,1)")
            for key,value in [('active_session_id','s'),('auto_title_enabled',False)]:db.execute('insert into app_settings values(?,?,1)',(key,json.dumps(value)))
            for seq,id,role,text in [(1,'old-user','user','old question'),(2,'old-answer','assistant','OLD_TEXT')]:
                db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'s',?,?,'complete','text',?,1,1)",(id,seq,role,text))
                db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,text) values(?,'s',?,0,'text',?)",(id,id,text))
        a=t.Native(BINARY,p)
        try:
            from test_reply_actions import action,read_clipboard
            a.edit('new question');a.tap(908,232);t.wait_for(lambda:a.diag()['receiving_reply'])
            s=a.diag();old=s['reply_actions']['rows']['old-answer'];assert old['visible'] and old['copy_enabled'] and not old['retry_enabled'] and not old['branch_enabled']
            action(a,'old-answer','copy');assert read_clipboard(a)=='OLD_TEXT'
            table='| Wide column | Value |\n|---|---|\n| '+'long table cell '*30+' | 12 |\n'
            Stream.commands.put(('text',table));t.wait_for(lambda:a.diag()['output_length']==len(table.encode()) and not a.diag()['chat_render_pending'])
            s=a.diag();assert s['following_latest']
            y=next(r['y']+10 for r in s['table_rows'] if 0<r['y']<180)
            for x in range(720,379,-20):a.send('touch',x=x,y=y,pressed=True);time.sleep(.025)
            a.send('touch',x=380,y=y,pressed=False)
            t.wait_for(lambda:not a.diag()['reader_interacting'])
            s=a.diag();assert any(r['scroll']>0 for r in s['table_rows']) and s['following_latest'] and not s['to_bottom_visible']
            shot(a,'wide-table-follow')
            Stream.commands.put(('done',''));t.wait_for(lambda:not a.diag()['generating'])
        finally:Stream.commands.put(('cut',''));a.close()
    print('Old reply copy remains enabled; wide-table horizontal scrolling retains follow.',flush=True)

def end_during_bottom_click():
    Stream.commands=queue.Queue()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-bottom-race-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        a=t.Native(BINARY,p)
        try:
            a.edit('race');a.tap(908,232);text='\n\n'.join('paragraph '+str(i) for i in range(25))
            Stream.commands.put(('text',text));t.wait_for(lambda:a.diag()['output_length']==len(text) and not a.diag()['chat_render_pending'])
            a.settings_swipe(150);t.wait_for(lambda:not a.diag()['reader_interacting'] and a.diag()['to_bottom_visible'])
            s=a.diag();x=s['to_bottom_x']+22;y=s['to_bottom_y']+22
            a.send('touch',x=x,y=y,pressed=True);time.sleep(.1)
            Stream.commands.put(('done',''));t.wait_for(lambda:not a.diag()['generating'])
            a.send('touch',x=x,y=y,pressed=False)
            t.wait_for(lambda:a.diag()['at_latest_bottom'] and not a.diag()['to_bottom_visible'] and not a.diag()['chat_render_pending'])
            assert a.diag()['following_latest']
        finally:Stream.commands.put(('cut',''));a.close()
    print('Reply completion between return-to-bottom press/release preserves the click.',flush=True)

def collapsed_thought_at_bottom():
    Stream.commands=queue.Queue()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-collapse-bottom-') as tmp:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();p=pathlib.Path(tmp);seed(p,server.server_port)
        with sqlite3.connect(p/'penchat.db') as db:
            for key,value in [('auto_title_enabled',False),('reasoning_display','during')]:db.execute('insert into app_settings values(?,?,1)',(key,json.dumps(value)))
        a=t.Native(BINARY,p)
        try:
            a.edit('thought collapse');a.tap(908,232)
            Stream.commands.put(('thought','\n\n'.join('thinking '+str(i) for i in range(65))))
            t.wait_for(lambda:a.diag()['reasoning_expanded_count']==1 and a.diag()['scroll_y']>1000)
            a.settings_swipe(140);t.wait_for(lambda:not a.diag()['reader_interacting'] and not a.diag()['following_latest'])
            text='\n\n'.join('answer '+str(i) for i in range(12));Stream.commands.put(('text',text))
            t.wait_for(lambda:a.diag()['output_length']==len(text) and not a.diag()['chat_render_pending'])
            s=a.diag();assert s['at_latest_bottom'] and not s['to_bottom_visible']
            t.wait_for(lambda:a.diag()['toolbar_y']==208)
            shot(a,'auto-collapse-composer-reachable')
            Stream.commands.put(('done',''));t.wait_for(lambda:not a.diag()['generating'])
        finally:Stream.commands.put(('cut',''));a.close()
    print('Automatic thought collapse landing at the latest bottom leaves the composer reachable.',flush=True)

if __name__=='__main__':
    run(960,266,'responses');run(936,280,'chat',True)
    for mode in ['done','stop','cut']:
        short_and_terminal(mode);waiting_terminal(mode)
    older_copy_and_horizontal()
    end_during_bottom_click()
    collapsed_thought_at_bottom()
