"""Deterministic Messages server; fake credentials and no external API calls."""
import http.server,json,time,sqlite3,pathlib
SIGNATURE='opaque-signature-fixture-never-log'
REDACTED='opaque-redacted-fixture-never-log'
class Handler(http.server.BaseHTTPRequestHandler):
 mode='ok';requests=[];delay=0
 def log_message(self,*args):pass
 def do_GET(self):
  type(self).requests.append((self.path,dict(self.headers),None))
  if self.path.endswith('/timeout'):time.sleep(1.2)
  second='after_id=' in self.path
  value={'data':[{'id':'claude-fixture-second' if second else 'claude-fixture'}],
         'has_more': not second or self.mode=='badpages','last_id':'cursor-one'}
  self.respond(200,json.dumps(value).encode(),'application/json')
 def respond(self,status,data,media):
  try:
   self.send_response(status);self.send_header('Content-Type',media);self.send_header('Content-Length',str(len(data)));self.send_header('request-id','messages-fixture');self.end_headers()
   if self.mode=='bytes':
    for byte in data:self.wfile.write(bytes([byte]));self.wfile.flush()
   else:self.wfile.write(data);self.wfile.flush()
  except (BrokenPipeError,ConnectionResetError):pass
 def do_POST(self):
  body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
  type(self).requests.append((self.path,dict(self.headers),body));mode=self.mode
  if mode=='cancel':time.sleep(.4)
  if mode in ('401','429','500'):
   return self.respond(int(mode),json.dumps({'type':'error','error':{'type':'fixture','message':'rejected fake-anthropic-key '+SIGNATURE}}).encode(),'application/json')
  if mode=='html':return self.respond(502,b'<html>gateway unavailable</html>','text/html')
  if mode=='large':return self.respond(200,b'data: '+b'x'*(4*1024*1024+1)+b'\n\n','text/event-stream')
  public=mode not in ('hidden','redacted','plain')
  content=[]
  if mode not in ('plain','redacted'):
   content.append({'type':'thinking','thinking':'公开思考' if public else '', 'signature':SIGNATURE})
  if mode!='plain':content.append({'type':'redacted_thinking','data':REDACTED})
  answer='中文回答。'
  serialized=json.dumps(body,ensure_ascii=False)
  if '[penchat-title]' in serialized:answer='Messages 标题'
  elif 'PenChat_CONTEXT_COMPRESSION_V1' in serialized:answer='历史摘要：保留事实、图片与未完成问题。'
  if mode=='refusal':answer='无法回答这个问题。'
  content.append({'type':'text','text':answer})
  reason={'length':'max_tokens','context':'model_context_window_exceeded','tool':'tool_use','pause':'pause_turn','refusal':'refusal','stop':'stop_sequence','unknownstop':'alien'}.get(mode,'end_turn')
  usage={'input_tokens':10,'cache_read_input_tokens':7,'cache_creation_input_tokens':3,'output_tokens':9,'output_tokens_details':{'thinking_tokens':2}}
  full={'id':'msg_fixture','type':'message','role':'assistant','model':body['model'],'content':content,'stop_reason':reason,'usage':usage}
  if mode=='badcontent':full['content']=[{'type':'text','text':42}]
  if mode=='empty':full['content']=[];full['stop_reason']=None
  if mode=='deep':
   full['extension']={}
   nested=full['extension']
   for _ in range(80):nested['n']={};nested=nested['n']
  if mode in ('json','wrongjson','badcontent','empty','deep') or body.get('stream') is False:
   return self.respond(200,json.dumps(full,ensure_ascii=False).encode(),'text/plain' if mode=='wrongjson' else 'application/json')
  frames=[{'type':'message_start','message':dict(full,content=[],stop_reason=None,usage=dict(usage,output_tokens=0))}]
  for i,block in enumerate(content):
   kind=block['type']
   initial=dict(block)
   if kind=='thinking':initial.update(thinking='',signature='')
   if kind=='text':initial['text']=''
   frames.append({'type':'content_block_start','index':i,'content_block':initial})
   if kind=='thinking':
    frames.extend([{'type':'content_block_delta','index':i,'delta':{'type':'thinking_delta','thinking':block['thinking']}},
                   {'type':'content_block_delta','index':i,'delta':{'type':'signature_delta','signature':SIGNATURE}}])
   if kind=='text':frames.append({'type':'content_block_delta','index':i,'delta':{'type':'text_delta','text':answer}})
   frames.append({'type':'content_block_stop','index':i})
  frames.extend([{'type':'message_delta','delta':{'stop_reason':reason},'usage':{'output_tokens':9}}, {'type':'message_stop'}])
  if mode=='final':frames.insert(-1,full)
  if mode=='lateusage':frames.insert(-1,{'type':'message_delta','delta':{},'usage':{'output_tokens':13}})
  if mode=='missing_stop':frames.pop()
  if mode=='cut':frames=frames[:3]
  if mode=='unclosed':frames=[f for f in frames if not(f['type']=='content_block_stop' and f['index']==len(content)-1)]
  if mode=='unknown':frames.insert(1,{'type':'vendor.notice','other':True})
  if mode=='invalid':frames[2]={'type':'content_block_delta','index':'wrong','delta':{'type':'text_delta','text':'bad'}}
  if mode=='sse_error':frames=frames[:3]+[{'type':'error','error':{'message':'failed fake-anthropic-key'}}]
  if mode=='chat':frames=[{'choices':[{'delta':{'content':'wrong protocol'},'finish_reason':'stop'}]}]
  if mode=='toolblock':frames[1]['content_block']={'type':'tool_use','id':'t','name':'tool','input':{}}
  if self.delay:
   self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
   try:
    for frame in frames:
     self.wfile.write(('event: '+frame['type']+'\ndata: '+json.dumps(frame,ensure_ascii=False)+'\n\n').encode());self.wfile.flush();time.sleep(self.delay)
   except (BrokenPipeError,ConnectionResetError):pass
   self.close_connection=True;return
  wire=': heartbeat\r\n\revent: ping\rdata: {"type":"ping"}\r\r'+''.join('event: '+f.get('type','extension')+'\ndata: '+json.dumps(f,ensure_ascii=False)+'\n\n' for f in frames)
  self.respond(200,wire.encode(),'text/plain' if mode=='wrongtype' else 'text/event-stream')

def seed(folder,port,host='127.0.0.1'):
 from test_reasoning_ui import seed as base
 base(folder,port)
 configure(folder,port,host)

def configure(folder,port,host='127.0.0.1'):
 metadata={'api':'anthropic','anthropic':{'auth':'x-api-key','version':'2023-06-01'},'default_effort':'high','credential_revision':'fixture-revision'}
 with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:
  db.execute('update providers set name=?,base_url=?,api_key=?,metadata_json=?',('Anthropic fixture',f'http://{host}:{port}/v1','fake-anthropic-key',json.dumps(metadata)))
  for mid,mode,levels in [('a','adaptive',{'none':'disabled','medium':'medium','high':'high','max':'max'}),('b','budget',{'medium':'4096','high':'8192'})]:
   caps={'reasoning':{'mode':mode,'levels':levels,'summary':True,'disable':True,'display_supported':mode=='adaptive'},'max_output_tokens':16384,'context_budget':65536}
   db.execute('update model_configs set capabilities_json=? where id=?',(json.dumps(caps),mid))
  for key,value in [('auto_title_enabled','false'),('auto_context_compression','false')]:db.execute('insert or replace into app_settings values(?,?,1)',(key,value))
