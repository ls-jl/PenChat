#!/usr/bin/env python3
"""Exercise the real curl/SSE pipeline against deterministic nonstandard servers."""
import http.server,json,os,pathlib,subprocess,threading,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_NETWORK_PROBE',ROOT/'build/host/penchat-network-probe'))
class Handler(http.server.BaseHTTPRequestHandler):
    mode='ok';requests=[]
    def log_message(self,*args):pass
    def do_GET(self):
        self.requests.append((self.path,dict(self.headers),None))
        if self.path=='/timeout':time.sleep(1.2)
        self.respond(200,b'{"data":[{"id":"model"}]}')
    def respond(self,status,data,media='text/event-stream'):
        try:
            self.send_response(status);self.send_header('Content-Type',media);self.send_header('Content-Length',str(len(data)));self.send_header('x-request-id','fake-secret' if self.mode=='bom' else 'fake-request');self.end_headers()
            if self.mode=='bytes':
                for c in data:self.wfile.write(bytes([c]));self.wfile.flush()
            else:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError):pass
    def do_POST(self):
        request=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append((self.path,dict(self.headers),request))
        mode=self.mode
        if mode in ('401','429','500'):
            return self.respond(int(mode),json.dumps({'error':{'message':'error fake-secret'}}).encode(),'application/json')
        if mode=='large':return self.respond(200,b'data: '+b'a'*(4*1024*1024+1)+b'\n\n')
        if mode=='html':return self.respond(502,b'<html>Bad gateway</html>','text/html')
        if mode=='cancel':time.sleep(.4)
        chat=self.path.endswith('/chat/completions')
        if chat:
            answer={'choices':[{'index':0,'message':{'role':'assistant','content':'中文 answer'},'finish_reason':'stop'}],'usage':{'prompt_tokens':12,'completion_tokens':3,'total_tokens':15}}
            finish=mode if mode in ('length','content_filter','tool_calls') else 'stop'
            frames=[{'choices':[{'index':0,'delta':{'content':'中文 answer'},'finish_reason':None}]},
                    {'choices':[{'index':0,'delta':{},'finish_reason':finish}]},
                    {'choices':[],'usage':answer['usage']}]
            if mode=='refusal':frames[0]['choices'][0]['delta']={'refusal':'无法回答'}
        else:
            answer={'object':'response','status':'completed','output':[{'type':'reasoning','summary':[{'type':'summary_text','text':'summary'}],'encrypted_content':'opaque'},
                 {'type':'message','role':'assistant','phase':'final_answer','status':'completed','content':[{'type':'output_text','text':'中文 answer'}]}],
                 'usage':{'input_tokens':12,'output_tokens':3,'total_tokens':15}}
            frames=[{'type':'response.output_text.delta','output_index':1,'content_index':0,'delta':'中文 answer'},
                    {'type':'response.completed','response':answer}]
            if mode=='incomplete':frames[-1]={'type':'response.incomplete','response':dict(answer,status='incomplete',incomplete_details={'reason':'max_output_tokens'})}
            if mode=='refusal':frames=[{'type':'response.refusal.delta','delta':'无法回答'}, {'type':'response.completed','response':{'status':'completed','output':[{'type':'message','content':[{'type':'refusal','refusal':'无法回答'}]}]}}]
        if mode in ('json','wrongjson') or request['stream'] is False:
            return self.respond(200,json.dumps(answer,ensure_ascii=False).encode(),'text/plain' if mode=='wrongjson' else 'application/json')
        if mode=='badcontent':
            frames=[{'choices':[{'index':0,'delta':{'content':123},'finish_reason':'stop'}]}] if chat else [{'type':'response.completed','response':dict(answer,output=[{'type':'message','content':[{'type':'output_text','text':123}]}])}]
        if mode=='invalid':frames=[{'type':'response.output_text.delta','delta':123}]
        if mode=='cut':frames=frames[:1]
        if mode=='unknown':frames.insert(0,{'type':'vendor.heartbeat','extra':True})
        wire=': heartbeat\r\n\r\n'+''.join('data: '+json.dumps(f,ensure_ascii=False)+'\r\n\r\n' for f in frames)
        if chat and mode not in ('eof','cut'):wire+='data: [DONE]\r\n\r\n'
        if mode=='bom':wire='\ufeff'+wire
        self.respond(200,wire.encode(),'text/plain' if mode=='wrongtype' else 'text/event-stream')
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
base=f'http://127.0.0.1:{server.server_port}'
results=[]
def run(mode='ok',chat=False,compat=False,task='',cancel=0,config=None):
    Handler.mode=mode;before=len(Handler.requests)
    metadata={'api':'chat' if chat else 'responses','compatibility':dict(enabled=compat,**(config or {}))}
    model={'name':'fixture','api_key':'fake-secret','base_url':base,'provider_id':'p','id':'m','provider_metadata':metadata,
           'headers_json':{'X-Test':'custom'},'system_prompt':'chat-only','effort_value':'high','reasoning_summary':True}
    if mode=='wrongjson':model['headers_json']['accept']='application/json'
    start=time.monotonic()
    p=subprocess.run([str(BINARY)],input=json.dumps({'model':model,'task':task,'cancel_ms':cancel}),text=True,capture_output=True,timeout=10)
    assert p.returncode==0,p.stderr
    events=json.loads(p.stdout);terminal=events[-1]
    assert len(Handler.requests)==before+1,(mode,events)
    assert Handler.requests[-1][1]['X-Test']=='custom'
    if mode=='wrongjson':assert [v for k,v in Handler.requests[-1][1].items() if k.lower()=='accept']==['application/json']
    if terminal['type']=='network_error':assert 'fake-secret' not in terminal['data']['error']
    state=next((e['data'] for e in events if e['type']=='response_state'),{})
    results.append((mode,chat,compat,terminal['type'],round((time.monotonic()-start)*1000)))
    return terminal,state,Handler.requests[-1][2]
try:
    for chat in (False,True):
        for mode in ('ok','bytes','wrongtype','unknown','bom'):
            end,state,request=run(mode,chat)
            assert end['type']=='done',(mode,chat,end)
            assert state['output']=='中文 answer'
            assert state['metadata']['usage']['total_tokens']==15
            assert state['metadata']['http']['request_id']==('[已隐藏]' if mode=='bom' else 'fake-request')
        end,state,_=run('refusal',chat);assert end['type']=='done' and state['output']=='无法回答'
        end,state,_=run('cut',chat);assert end['type']=='network_error'
        end,state,_=run('json',chat);assert end['type']=='network_error'
        for mode in ('json','wrongjson'):
            end,state,_=run(mode,chat,True);assert end['type']=='done' and state['output']=='中文 answer'
        end,state,req=run('ok',chat,True,config={'streaming':False,'usage':False,'summary':False,'replay':False,'http1':True})
        assert end['type']=='done' and req['stream'] is False and 'stream_options' not in req and 'include' not in req
        assert not state['metadata'].get('response_output_items') and not state['metadata'].get('response_reasoning_items')
        if not chat:assert req['reasoning']=={'effort':'high'}
        for mode in ('401','429','500','html','invalid','badcontent','large'):
            end,state,_=run(mode,chat);assert end['type']=='network_error'
        end,state,_=run('cancel',chat,cancel=50);assert end['type']=='cancelled'
    for mode in ('length','content_filter','tool_calls'):
        end,state,_=run(mode,True);assert end['type']=='network_error' and mode in end['data']['error']
    end,state,_=run('eof',True);assert end['type']=='network_error'
    end,state,_=run('eof',True,True);assert end['type']=='done'
    end,state,_=run('incomplete');assert end['type']=='network_error' and 'max_output_tokens' in end['data']['error']
    end,state,_=run(task='models');assert end['type']=='http' and end['data']['status']==200
    end,state,_=run(task='timeout');assert end['type']=='http' and '超时' in end['data']['error']
    end,state,req=run(task='title');assert end['type']=='title' and 'chat-only' not in json.dumps(req)
    print(json.dumps({'cases':len(results),'results':results},ensure_ascii=False))
finally:server.shutdown()
