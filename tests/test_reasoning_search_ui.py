#!/usr/bin/env python3
"""Tool boundaries retain public reasoning; fake HTTP and fake search worker only."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as ui
from image_gestures import capture
OUT=ui.ROOT/'.work/reasoning-search';OUT.mkdir(parents=True,exist_ok=True)
QUERY='词典笔 [查询内容]'
class Server(http.server.BaseHTTPRequestHandler):
 calls=0;answer=threading.Event()
 def log_message(self,*args):pass
 def do_POST(self):
  self.rfile.read(int(self.headers['Content-Length']));type(self).calls+=1;round=type(self).calls
  self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
  def emit(delta,finish=None):
   self.wfile.write(('data: '+json.dumps({'choices':[{'index':0,'delta':delta,'finish_reason':finish}]},ensure_ascii=False)+'\n\n').encode());self.wfile.flush()
  try:
   if round==1:
    emit({'reasoning_content':'准备检索资料。'});time.sleep(.2)
    emit({'tool_calls':[{'index':0,'id':'search-1','type':'function','function':{'name':'web_search','arguments':json.dumps({'query':QUERY})}}]},'tool_calls')
   else:
    emit({'reasoning_content':'资料已取得，继续分析。'})
    assert self.answer.wait(15),'Test did not release answer'
    emit({'content':'根据资料，回答完成。'},'stop')
   self.wfile.write(b'data: [DONE]\n\n');self.wfile.flush()
  except (BrokenPipeError,ConnectionResetError):pass
  self.close_connection=True
for manual in [False,True]:
 with tempfile.TemporaryDirectory(prefix='reasoning-search-ui-') as folder, http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server:
  root=pathlib.Path(folder);workspace=root/'workspace';binary=workspace/'assets/bin';binary.mkdir(parents=True)
  for child in (ui.ROOT/'assets').iterdir():
   if child.name!='bin':(workspace/'assets'/child.name).symlink_to(child.resolve(),target_is_directory=child.is_dir())
  gate=root/'search-release';worker=binary/'penchat-web-search'
  worker.write_text('''#!/usr/bin/env python3
import json,pathlib,socket,struct,sys,time
s=socket.socket(fileno=int(sys.argv[1]))
def read(n):
 b=b''
 while len(b)<n:
  v=s.recv(n-len(b))
  if not v:sys.exit(0)
  b+=v
 return b
while True:
 q=json.loads(read(struct.unpack('!I',read(4))[0]));name=q['name']
 if name=='hello':value={'component':'0.14.0','protocol':1}
 else:
  deadline=time.monotonic()+15
  while not pathlib.Path('''+repr(str(gate))+''').exists():
   if time.monotonic()>deadline:sys.exit(2)
   time.sleep(.02)
  value={'query':q['arguments'].get('query',''),'results':[{'url':'https://example.org/fixture','title':'本地测试资料','content':'只使用本地假资料。'}]}
 data=json.dumps({'request':q['request'],'ok':True,'result':value}).encode();s.sendall(struct.pack('!I',len(data))+data)
''');worker.chmod(0o700)
  data=root/'data';data.mkdir();schema=(ui.ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
  with sqlite3.connect(data/'penchat.db') as db:
   db.executescript(schema)
   db.execute("insert into providers(id,name,type,base_url,api_key,metadata_json,created_at,updated_at,is_default) values('fixture-provider','Fixture','openai-compatible',?,'test-only',?,1,1,1)",(f'http://127.0.0.1:{server.server_port}/v1',json.dumps({'api':'chat'})))
   db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,created_at,updated_at,is_default) values('fixture-model','fixture-provider','fixture','Fixture','chat',1,1,1)")
   for key,value in [('reasoning_display','during'),('new_chat_web','on'),('auto_title_enabled',False)]:db.execute('insert into app_settings values(?,?,1)',(key,json.dumps(value)))
  Server.calls=0;Server.answer=threading.Event();ui.port=server.server_port
  threading.Thread(target=server.serve_forever,daemon=True).start()
  app=ui.Native(ui.ROOT/'build/host/penchat-lvgl',data,api='chat',workspace=workspace)
  try:
   app.edit('测试思考期间检索');d=app.diag();app.tap(d['send_center_x2']//2,d['send_center_y2']//2)
   def control():
    rows=app.diag()['reasoning_controls'];return rows[0] if rows else {}
   ui.wait_for(lambda:'正在搜索' in control().get('search_text',''))
   d=app.diag();c=control();assert c['expanded'] and QUERY in c['search_text'] and d['web_controls_count']==0
   capture(app,OUT/(('manual-' if manual else 'auto-')+'running.png'))
   if manual:
    b=c['arrow'];app.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    ui.wait_for(lambda:not control()['expanded'])
   gate.touch()
   ui.wait_for(lambda:Server.calls==2 and '搜索了' in control().get('search_text',''))
   assert control()['expanded'] is (not manual)
   assert QUERY in control()['search_text'] and app.diag()['web_controls_count']==0
   Server.answer.set();ui.wait_for(lambda:not app.diag()['generating'])
   assert not control()['expanded']
   assert app.diag()['web_controls_count']==1
   with sqlite3.connect(data/'penchat.db') as db:
    reason,metadata=db.execute("select reasoning_text,metadata_json from chat_messages where role='assistant'").fetchone()
    assert '准备检索资料' in reason and '搜索了' not in reason and '正在搜索' not in reason
    meta=json.loads(metadata);assert meta['web_pending'] is False and meta['web_records'][0]['arguments']['query']==QUERY
   print('Search stays in reasoning, manual override='+str(manual),flush=True)
  finally:
   gate.touch();Server.answer.set();app.close();server.shutdown()
