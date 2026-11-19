#!/usr/bin/env python3
"""Image-only first sends must name their subject, not the attachment marker."""
import http.server,json,os,pathlib,sqlite3,struct,tempfile,threading,zlib
import test_lvgl_app as t
from test_reasoning_ui import seed
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
class Server(t.Server):
  requests=[];title='图片';failure=False
  def do_POST(self):
    request=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
    naming='[penchat-title]' in json.dumps(request);self.requests.append((naming,request))
    if naming and self.failure:
      self.send_response(503);self.send_header('Content-Length','0');self.end_headers();return
    self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
    text=self.title if naming else '水的电解反应。图片中展示了氢气和氧气的生成过程。'
    self.wfile.write(('data: '+json.dumps({'type':'response.output_text.delta','delta':text},ensure_ascii=False)+'\n\ndata: {"type":"response.completed"}\n\n').encode());self.wfile.flush();self.close_connection=True

def run(server,title,failure=False,caption=''):
  Server.title=title;Server.failure=failure;Server.requests=[]
  with tempfile.TemporaryDirectory(prefix='penchat-image-title-') as directory:
    root=pathlib.Path(directory);seed(directory,server.server_port)
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data))
    image=root/'photo.png';image.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',10,10,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([80,150,210])*10)*10))+chunk(b'IEND',b''))
    with sqlite3.connect(root/'penchat.db') as db:
      db.execute("insert or replace into app_settings values('auto_title_enabled','true',1)")
      db.execute('update providers set base_url=?',('http://127.0.0.1:'+str(server.server_port)+'/v1',))
      db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('fixture','[图片]','active',1,1,1)")
      db.execute("insert into app_settings values('active_session_id','\"fixture\"',1)")
      db.execute("update model_configs set capabilities_json='{}'")
      db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('photo','fixture',1,'user','complete','image','',1,1)")
      if caption:db.execute("update chat_messages set output_text=? where id='photo'",(caption,))
      db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,width,height,created_at) values('photo','fixture','photo','image','album',?,?,10,10,1)",(str(image),str(image)))
      db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('photo','fixture','photo',0,'image','photo')")
    app=t.Native(BINARY,root)
    def info():
      with sqlite3.connect(root/'penchat.db') as db:return db.execute("select title,metadata_json from chat_sessions where id='fixture'").fetchone()
    try:
      bounds=app.diag()['composer_send_bounds'] if 'composer_send_bounds' in app.diag() else None
      if bounds:app.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
      else:app.tap(908,232)
      t.wait_for(lambda:not app.diag()['generating'] and len(Server.requests)>=2)
      t.wait_for(lambda:json.loads(info()[1]).get('title_state')=='complete')
      expected='水的电解反应' if failure or title in ('图片','[图片]','') else title
      assert info()[0]==expected,info()
      assert sum(naming for naming,_ in Server.requests)==1,'Never retry an auxiliary title request automatically'
      material=next(request for naming,request in Server.requests if naming)
      assert (caption if caption else '具体内容命名') in json.dumps(material,ensure_ascii=False)
      assert image.exists()
    finally:app.close()
  print({'returned_title':title,'request_failed':failure,'stored_title':expected},flush=True)
if __name__=='__main__':
  with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server:
    t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start()
    for title,failure in [('图片',False),('[图片]',False),('水分解实验',False),('',False),('',True)]:run(server,title,failure)
    run(server,'分析实验照片',caption='请分析实验照片中的电解装置')
    server.shutdown()
