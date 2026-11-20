#!/usr/bin/env python3
"""Official composer buttons use matched confirmation; no gesture can auto-send."""
import http.server,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as ui
ui.Server.requests=[]
with http.server.ThreadingHTTPServer(('127.0.0.1',0),ui.Server) as server,tempfile.TemporaryDirectory(prefix='official-actions-') as folder:
 ui.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start()
 a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',pathlib.Path(folder),model='cancel',official_adapter=False,screen={'width':568,'height':1210})
 try:
  def open_official():
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   ui.wait_for(lambda:a.diag()['editor']['active']);a.editor_action('official');r=a.receive('keyboard.open')
   a.send('keyboard.layout',requestId=r['requestId'],height=590,foreground=True)
   return r['requestId']
  def touch(x,y,down=True,**extra):
   a.send('keyboard.layout',requestId=request,height=590,foreground=True)
   a.send('touch',x=x,y=y,pressed=down,**extra);time.sleep(.03)
  def tap(kind):
   d=a.diag();b=d['attachment_button'] if kind=='attachments' else [d['send_center_x2']//2,d['send_center_y2']//2]*2
   x,y=(b[0]+b[2])//2,(b[1]+b[3])//2;touch(x,y);touch(x,y,False)
  def actions():a.diag();return [e for e in a.pending if e.get('type')=='keyboard.commit-action']
  request=open_official();tap('send');assert not actions()
  a.send('keyboard.change',requestId=request,text='初稿',cursorIndex=2)
  b=a.diag()['attachment_button'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
  touch(x,y);touch(x+60,y);touch(x,y,False);assert not actions()
  touch(x,y);touch(x,y,False,cancelled=True);assert not actions()
  touch(x,y);touch(x,y,points=[{'id':1,'x':x,'y':y},{'id':2,'x':x+10,'y':y}]);touch(x,y,False);assert not actions()
  touch(300,800);touch(300,800,False)
  a.send('keyboard.change',requestId=request,text='初稿',cursorIndex=2) # Cursor heartbeat is not a text acknowledgement.
  tap('attachments');ui.wait_for(lambda:a.diag()['attachments'])
  assert a.diag()['keyboard'] and a.diag()['editor']['length']==2 and not actions()
  assert a.diag()['attachment_viewport'][3]<a.diag()['display_top_height']
  # Keyboard-origin taps never dismiss the popup or enter LVGL controls.
  touch(300,800);touch(300,800,False);assert a.diag()['attachments']
  touch(560,100);touch(560,100,False);ui.wait_for(lambda:not a.diag()['attachments'])
  assert a.diag()['keyboard'] and a.diag()['editor']['length']==2
  # Direct dismissal commits the live proxy value, without a send action.
  a.send('keyboard.result',requestId=request,confirmed=True,retained=True,text='已同步最新输入')
  ui.wait_for(lambda:not a.diag()['keyboard']);assert a.diag()['draft_length']==7 and not ui.Server.requests
  a.send('keyboard.result',requestId=request,confirmed=True,retained=True,text='stale')
  assert a.diag()['draft_length']==7
  request=open_official();tap('attachments')
  b=a.diag()['attachment_actions']['models'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
  touch(x,y);touch(x,y,False)
  chosen=a.receive('keyboard.commit-action');assert chosen['action']=='menu'
  a.send('keyboard.result',requestId=request,actionId=chosen['actionId'],confirmed=True,text='已同步最新输入')
  ui.wait_for(lambda:not a.diag()['keyboard'] and a.diag()['attachments'])
  assert a.diag()['draft_length']==7
  a.tap(560,100);ui.wait_for(lambda:not a.diag()['attachments'])
  request=open_official();a.send('keyboard.change',requestId=request,text='一次发送',cursorIndex=4)
  tap('send');action=a.receive('keyboard.commit-action');assert action['action']=='send' and not ui.Server.requests
  a.send('keyboard.action.error',requestId=request,actionId=action['actionId']);assert a.diag()['keyboard']
  tap('send');retry=a.receive('keyboard.commit-action');assert retry['actionId']!=action['actionId']
  a.send('keyboard.result',requestId=request,actionId=retry['actionId'],confirmed=True,text='一次发送')
  ui.wait_for(lambda:len(ui.Server.requests)==1)
  old_request=request;request=open_official();editing=a.diag()['editor']['session']
  a.send('keyboard.result',requestId=old_request,actionId=retry['actionId'],confirmed=True,text='重复')
  assert a.diag()['keyboard'] and a.diag()['editor']['session']==editing
  touch(300,800);touch(300,800,False);assert a.diag()['editor']['official_action']['uncommitted']
  tap('send') # The current glyph is Stop; it must not commit or discard IME input.
  ui.wait_for(lambda:not a.diag()['generating'])
  assert a.diag()['keyboard'] and a.diag()['editor']['session']==editing and not actions()
  a.send('keyboard.result',requestId=request,confirmed=False)
  ui.wait_for(lambda:not a.diag()['keyboard'] and not a.diag()['editor']['active'])
  assert len(ui.Server.requests)==1
  with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:
   rows=db.execute("select output_text from chat_messages where role='user'").fetchall();assert rows==[('一次发送',)],rows
  print('OFFICIAL_PLUS_SEND_LATEST_TEXT_CANCEL_MULTI_DUPLICATE_ERROR_RETRY_OK')
 finally:a.close();server.shutdown()
