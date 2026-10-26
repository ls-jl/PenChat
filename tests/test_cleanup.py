#!/usr/bin/env python3
"""Real process exit while a remote image stalls; measure idle background ticks."""
import http.server,json,pathlib,socket,sqlite3,tempfile,threading,time,os
import test_lvgl_app as t
ROOT=t.ROOT;BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
started=threading.Event();release=threading.Event()
class Server(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        started.set();self.send_response(200);self.send_header('Content-Type','image/png');self.end_headers();self.wfile.flush();release.wait(5)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);server.daemon_threads=True
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    for cause in ('exit','eof','signal'):
        with tempfile.TemporaryDirectory(prefix='penchat-cleanup-') as directory:
            p=pathlib.Path(directory);t.port=server.server_port
            # Seed a real message so the UI requests an image on startup.
            schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
            with sqlite3.connect(p/'penchat.db') as db:
                db.executescript(schema)
                db.execute("insert into app_settings values('active_session_id','\"fixture\"',1)")
                db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('fixture','fixture','active',1,1,1)")
                db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('image','fixture',0,'assistant','complete','text',?,1,1)",(f'![stalled](http://127.0.0.1:{server.server_port}/image.png)',))
            started.clear();a=t.Native(BINARY,p,seed_model=False,official_adapter=False)
            try:
                assert started.wait(3)
                stage=p/'.message-edit';stage.mkdir(exist_ok=True)
                late=stage/'image_123_456.png';late.write_bytes(b'late staged image')
                preserved=stage/'unknown-user-file';preserved.write_bytes(b'keep')
                if cause=='exit':a.send('exit')
                elif cause=='eof':a.parent.close()
                else:a.process.terminate()
                now=time.monotonic();code=a.process.wait(timeout=3)
                assert code==0,(cause,code,(p/'runtime.log').read_text(errors='replace')[-16000:])
                elapsed=time.monotonic()-now;assert elapsed<2.0,(cause,elapsed)
                with sqlite3.connect(p/'penchat.db') as db:assert db.execute('pragma integrity_check').fetchone()[0]=='ok'
                assert not late.exists() and preserved.read_bytes()==b'keep'
                print(f'Cleanup {cause}: stalled image cancelled, joined and DB intact in {elapsed:.3f}s')
            finally:
                a.parent.close()
                if a.process.poll() is None:a.process.kill();a.process.wait()
                a.log.close()
finally:release.set();server.shutdown();server.server_close()
