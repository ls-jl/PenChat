#!/usr/bin/env python3
"""Real SSE transport: silence/keepalive after thinking, terminal loss and cancellation."""
import http.server,json,os,pathlib,subprocess,threading,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_NETWORK_PROBE',ROOT/'build/host/penchat-network-probe'))
OUT=pathlib.Path(os.environ.get('PENCHAT_DEEPSEEK_ARTIFACTS',ROOT/'.work/deepseek-stream'));OUT.mkdir(parents=True,exist_ok=True)
class Server(http.server.BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1';mode='normal';requests=0
  def log_message(self,*args):pass
  def do_POST(self):
    self.rfile.read(int(self.headers['Content-Length']));type(self).requests+=1
    self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
    mode=type(self).mode
    def emit(value):self.wfile.write(('data: '+json.dumps(value,ensure_ascii=False)+'\n\n').encode());self.wfile.flush()
    def delta(**values):emit({'choices':[{'index':0,'delta':values,'finish_reason':None}]})
    try:
      if mode=='queue':
        for _ in range(5):self.wfile.write(b': keep-alive\n\n');self.wfile.flush();time.sleep(.1)
      delta(reasoning_content='保留这段思考。')
      if mode in ('stalled','cancel'):
        until=time.monotonic()+130
        while time.monotonic()<until:
          self.wfile.write(b': keep-alive\n\n');self.wfile.flush();time.sleep(.2)
        return
      if mode=='error':emit({'error':{'message':'upstream interrupted','code':'inference_timeout'},'choices':[]});time.sleep(1);return
      time.sleep(.25)
      if mode!='reasoning-only':delta(content='正常回答。')
      emit({'choices':[{'index':0,'delta':{},'finish_reason':'stop'}]})
      if mode=='missing-end':
        time.sleep(7);return
      self.wfile.write(b'data: [DONE]\n\n');self.wfile.flush()
    except (BrokenPipeError,ConnectionResetError):pass
    finally:self.close_connection=True

def run(server,mode):
  Server.mode=mode;before=Server.requests
  model={'name':'deepseek-reasoner','provider_id':'fake','id':'fake-model','api_key':'fake-test-key',
         'base_url':f'http://127.0.0.1:{server.server_port}/v1','provider_metadata':{'api':'chat','model_adapter':'deepseek'}}
  request={'model':model,'cancel_ms':300 if mode=='cancel' else 0}
  start=time.monotonic();p=subprocess.run([str(BINARY)],input=json.dumps(request),text=True,capture_output=True,timeout=145)
  elapsed=time.monotonic()-start;assert p.returncode==0,p.stderr
  events=json.loads(p.stdout);end=events[-1];state=next(e['data'] for e in events if e['type']=='response_state')
  assert state['reasoning']=='保留这段思考。' and Server.requests==before+1
  assert 'fake-test-key' not in json.dumps(events,ensure_ascii=False)
  if mode in ('normal','queue'):assert end['type']=='done' and state['output']=='正常回答。'
  elif mode=='cancel':assert end['type']=='cancelled' and elapsed<3
  else:
    assert end['type']=='network_error',end
    if mode=='missing-end':assert '结束标记' in end['data']['error'] and 5<=elapsed<7
    elif mode=='stalled':assert '120 秒' in end['data']['error'] and 120<=elapsed<125 and state['metadata']['stream_waiting']
    elif mode=='reasoning-only':assert '未返回回答正文' in end['data']['error']
    elif mode=='error':assert 'upstream interrupted' in end['data']['error'] and elapsed<3
  result={'mode':mode,'elapsed_seconds':round(elapsed,3),'terminal':end['type'],'reasoning_preserved':True,'single_request':True}
  print(result,flush=True);return result
if __name__=='__main__':
  with http.server.ThreadingHTTPServer(('127.0.0.1',0),Server) as server:
    threading.Thread(target=server.serve_forever,daemon=True).start()
    results=[run(server,mode) for mode in ('normal','queue','error','reasoning-only','missing-end','cancel','stalled')]
    (OUT/'deepseek-stall-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n');server.shutdown()
