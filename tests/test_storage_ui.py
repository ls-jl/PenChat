#!/usr/bin/env python3
"""A busy database must not stop the native editor while a reply streams."""
import http.server,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),t.Server)
t.port=server.server_port
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
 with tempfile.TemporaryDirectory(prefix='penchat-storage-ui-') as folder:
  a=t.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl')),folder,model='scroll',official_adapter=False)
  lock=None
  try:
   a.close()
   with sqlite3.connect(a.directory/'penchat.db') as db:
    db.execute("INSERT OR REPLACE INTO app_settings VALUES('auto_title_enabled','false',1)")
   a=t.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl')),folder,model='scroll',official_adapter=False)
   # Use the visible official fallback solely to seed the draft.
   a.tap(500,232);a.tap(860,236);request=a.receive('keyboard.open')
   a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='数据库忙时仍能输入')
   time.sleep(.1);before=a.diag()['storage']['transactions'];a.tap(908,232)
   t.wait_for(lambda:len(a.read_messages())==2)
   lock=sqlite3.connect(a.directory/'penchat.db');lock.execute('BEGIN IMMEDIATE')
   time.sleep(.65) # first 500ms checkpoint is now waiting for the write lock
   started=time.monotonic()
   a.tap(500,232);a.tap(860,236);request=a.receive('keyboard.open')
   latency=time.monotonic()-started
   assert latency<1.5,latency  # SQLite busy timeout is 3s; UI must not await it.
   lock.rollback();lock.close();lock=None
   a.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='')
   time.sleep(.1);a.tap(630,100)
   t.wait_for(lambda:a.read_messages()[-1][1]=='complete',timeout=12)
   t.wait_for(lambda:not a.diag()['generating'])
   after=a.diag()['storage']['transactions']
   assert after-before<=8,(before,after)
   with sqlite3.connect(a.directory/'penchat.db') as db:
    assert not db.execute("SELECT 1 FROM message_parts p JOIN chat_messages m ON m.id=p.message_id WHERE p.part_type='text' AND p.text!=m.output_text").fetchall()
   print('Busy DB editor response %.3fs; request transactions %d; atomic text copies match'%(latency,after-before))
  finally:
   if lock:lock.rollback();lock.close()
   a.close()
finally:server.shutdown();server.server_close()
