#!/usr/bin/env python3
"""A paused DeepSeek stream becomes visibly waiting and resumes without resend."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
class Server(http.server.BaseHTTPRequestHandler):
  requests=0
  def log_message(self,*args):pass
  def do_POST(self):
    self.rfile.read(int(self.headers['Content-Length']));type(self).requests+=1
    self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
    def emit(delta,finish=None):self.wfile.write(('data: '+json.dumps({'choices':[{'index':0,'delta':delta,'finish_reason':finish}]},ensure_ascii=False)+'\n\n').encode());self.wfile.flush()
    try:
      emit({'reasoning_content':'这段思考需要保留。'});time.sleep(18)
      emit({'reasoning_content':'等待结束，继续推理。'});emit({'content':'服务恢复后的回答。'},'stop')
      self.wfile.write(b'data: [DONE]\n\n');self.wfile.flush()
    except (BrokenPipeError,ConnectionResetError):pass
    self.close_connection=True
out=pathlib.Path(os.environ.get('PENCHAT_DEEPSEEK_ARTIFACTS',t.ROOT/'.work/deepseek-stream'));out.mkdir(parents=True,exist_ok=True)
with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server,tempfile.TemporaryDirectory(prefix='penchat-waiting-ui-') as directory:
  t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start();root=pathlib.Path(directory);seed(directory,server.server_port)
  with sqlite3.connect(root/'penchat.db') as db:
    db.execute('update providers set metadata_json=?',(json.dumps({'api':'chat','model_adapter':'deepseek'}),))
    db.execute("insert into app_settings values('auto_title_enabled','false',1)")
  a=t.Native(t.ROOT/'build/host/penchat-lvgl',root)
  def row():
    with sqlite3.connect(root/'penchat.db') as db:
      value=db.execute("select status,output_text,metadata_json from chat_messages where role='assistant' order by seq desc limit 1").fetchone()
      return value if value else ('','','{}')
  try:
    a.edit('本地等待测试');a.tap(908,232);t.wait_for(lambda:a.diag()['generating'])
    t.wait_for(lambda:bool(a.diag()['reasoning_controls']))
    if not a.diag()['reasoning_controls'][0]['expanded']:
      b=a.diag()['reasoning_controls'][0]['arrow'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    t.wait_for(lambda:json.loads(row()[2]).get('stream_waiting'),timeout=20)
    assert a.diag()['reasoning_header_count']==1 and a.diag()['generating']
    assert a.diag()['reasoning_controls'][0]['expanded']
    capture(a,out/'deepseek-waiting-ui.png')
    t.wait_for(lambda:not a.diag()['generating'],timeout=10)
    status,text,metadata=row();metadata=json.loads(metadata)
    assert status=='complete' and text=='服务恢复后的回答。' and not metadata['stream_waiting']
    assert Server.requests==1
    capture(a,out/'deepseek-resumed-ui.png')
  finally:a.close();server.shutdown()
print('DeepSeek waiting header at 15 seconds, reasoning retained, resumed output, one request and completed DB save passed.')
