#!/usr/bin/env python3
"""Targeted tool regressions. Opt-in: does not join the ordinary development build.
Uses a local AI fixture and an isolated fake worker; never contacts search engines.
"""
import base64, hashlib, http.server, json, os, pathlib, socket, struct, subprocess
import tempfile, threading, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
PROBE=ROOT/'build/host/penchat-network-probe'
PNG=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a7V8AAAAASUVORK5CYII=')
class Fixture(http.server.BaseHTTPRequestHandler):
    requests=[]; mode='normal'; protocol='responses'
    def log_message(self,*args): pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append(body)
        number=len(self.requests); chat=self.protocol=='chat'; anthropic=self.protocol=='anthropic'
        arguments={'query':'fixture search'}
        if self.mode=='image': name='web_read_image';arguments={'page_id':1,'link_id':1,'inspect':False}
        else:name='web_search'
        if number==1 and self.mode not in ('direct','syntax'):
            if anthropic: answer={'type':'message','role':'assistant','content':[{'type':'tool_use','id':'call_1','name':name,'input':arguments}],'stop_reason':'tool_use','usage':{'input_tokens':8,'output_tokens':2}}
            elif chat: answer={'choices':[{'index':0,'message':{'role':'assistant','content':None,'tool_calls':[{'id':'call_1','type':'function','function':{'name':name,'arguments':json.dumps(arguments)}}]},'finish_reason':'tool_calls'}],'usage':{'prompt_tokens':8,'completion_tokens':2}}
            else:answer={'status':'completed','output':[{'type':'function_call','id':'fc1','call_id':'call_1','name':name,'arguments':json.dumps(arguments)}],'usage':{'input_tokens':8,'output_tokens':2}}
        else:
            text='![fixture](penchat-image:img_1)' if self.mode=='image' else 'Fixture answer [source](https://example.org/)'
            if self.mode=='syntax':text='Useful preface\n<｜｜DSML｜｜ calls>\n<｜｜DSML｜｜ invoke name="web_open">\n{"url":"https://example.org/"}'
            if anthropic:answer={'type':'message','role':'assistant','content':[{'type':'text','text':text}],'stop_reason':'end_turn','usage':{'input_tokens':20,'output_tokens':4}}
            elif chat:answer={'choices':[{'index':0,'message':{'role':'assistant','content':text},'finish_reason':'stop'}],'usage':{'prompt_tokens':20,'completion_tokens':4}}
            else:answer={'status':'completed','output':[{'type':'message','status':'completed','role':'assistant','content':[{'type':'output_text','text':text}]}],'usage':{'input_tokens':20,'output_tokens':4}}
        if body['stream']:
            frames=[]
            if anthropic:
                frames=[{'type':'message_start','message':{'content':[],'usage':answer['usage']}}]
                for i,block in enumerate(answer['content']):
                    initial=dict(block)
                    if block['type']=='tool_use':initial['input']={}
                    frames.append({'type':'content_block_start','index':i,'content_block':initial})
                    if block['type']=='tool_use':frames.append({'type':'content_block_delta','index':i,'delta':{'type':'input_json_delta','partial_json':json.dumps(block['input'])}})
                    frames.append({'type':'content_block_stop','index':i})
                frames += [{'type':'message_delta','delta':{'stop_reason':answer['stop_reason']},'usage':answer['usage']},{'type':'message_stop'}]
            elif chat:
                choice=answer['choices'][0];message=choice['message']
                if 'tool_calls' in message:
                    tool=message['tool_calls'][0];args=tool['function']['arguments'];first=dict(tool,index=0);first['function']={'name':name,'arguments':''}
                    frames=[{'choices':[{'index':0,'delta':{'tool_calls':[first]},'finish_reason':None}]}]
                    for letter in args:frames.append({'choices':[{'index':0,'delta':{'tool_calls':[{'index':0,'function':{'arguments':letter}}]},'finish_reason':None}]})
                else:frames=[{'choices':[{'index':0,'delta':{'content':message['content']},'finish_reason':None}]}]
                frames += [{'choices':[{'index':0,'delta':{},'finish_reason':choice['finish_reason']}]},{'choices':[],'usage':answer['usage']}]
            else:frames=[{'type':'response.completed','response':answer}]
            data=''.join('data: '+json.dumps(frame)+'\n\n' for frame in frames)
            if chat:data+='data: [DONE]\n\n'
            data=data.encode();media='text/event-stream'
        else:data=json.dumps(answer).encode();media='application/json'
        self.send_response(200);self.send_header('Content-Type',media);self.send_header('Content-Length',str(len(data)));self.end_headers()
        # Deliberately split UTF-8 and JSON at every byte.
        for byte in data:self.wfile.write(bytes([byte]))
class Tools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
    @classmethod
    def tearDownClass(cls):cls.server.shutdown();cls.server.server_close()
    def run_case(self,protocol,stream=True,mode='normal'):
        with tempfile.TemporaryDirectory() as temp:
            path=pathlib.Path(temp);worker=path/'worker'
            # Test worker is intentionally isolated and implements only the IPC contract.
            worker.write_text('#!/usr/bin/env python3\n'+'''import json,socket,struct,sys,base64
s=socket.socket(fileno=int(sys.argv[1]))
def read(n):
 b=b''
 while len(b)<n:
  chunk=s.recv(n-len(b))
  if not chunk:sys.exit(0)
  b+=chunk
 return b
while True:
 n=struct.unpack('!I',read(4))[0];q=json.loads(read(n));name=q['name']
 if name=='hello':r={'component':'0.14.0','protocol':1}
 elif name=='web_read_image':r={'url':'https://example.org/image.png','source_url':'https://example.org/','source_title':'fixture','image':{'complete':True,'mime_type':'image/png','data':''' +repr(base64.b64encode(PNG).decode())+'''}}
 else:r={'results':[{'url':'https://example.org/','title':'Fixture','content':'Reference text'}]}
 data=json.dumps({'request':q['request'],'ok':True,'result':r}).encode();s.sendall(struct.pack('!I',len(data))+data)
''');worker.chmod(0o700)
            Fixture.requests=[];Fixture.protocol=protocol;Fixture.mode=mode
            model={'id':'fixture','name':'fixture','provider_id':'fixture','base_url':f'http://127.0.0.1:{self.server.server_port}', 'api_key':'fake-key','system_prompt':'Custom session preference: answer in English.','web_enabled':True,'web_binary':str(worker),'web_asset_root':str(path/'assets'), 'provider_metadata':{'api':protocol,'compatibility':{'enabled':True,'streaming':stream}},'capabilities_json':{'vision':False,'reasoning':{'mode':'off','levels':{}}}}
            request={'model':model}
            if mode=='syntax':request['input']=[{'role':'user','content':[{'type':'input_text','text':'Explain the exact DSML tool invocation syntax and show an example.'}]}]
            data=json.loads(subprocess.check_output([str(PROBE)],input=json.dumps(request).encode()))
            for request in Fixture.requests:
                prompt=request['messages'][0]['content'] if protocol=='chat' else request['system' if protocol=='anthropic' else 'instructions']
                self.assertEqual(prompt.count('PenChat_APPLICATION_RULES_V1'),1)
                self.assertEqual(prompt.count(model['system_prompt']),1)
            if mode=='syntax':
                self.assertEqual(len(Fixture.requests),1)
                self.assertEqual(sum(e['type']=='done' for e in data),1)
                self.assertFalse(any(e['type'] in ('network_error','exception') for e in data),data)
                state=next(e['data'] for e in data if e['type']=='response_state')
                self.assertEqual(state['output'],'Useful preface\n<｜｜DSML｜｜ calls>\n<｜｜DSML｜｜ invoke name=\"web_open\">\n{\"url\":\"https://example.org/\"}')
                self.assertNotIn('tool_text_rejected',state['metadata'])
                self.assertIn('DSML',state['output'])
                self.assertEqual(len(state['metadata']['web_records']),0)
                return
            self.assertEqual(sum(e['type']=='done' for e in data),1,data)
            self.assertFalse(any(e['type'] in ('network_error','exception') for e in data),data)
            state=next(e['data'] for e in data if e['type']=='response_state')
            self.assertEqual(len(Fixture.requests),1 if mode=='direct' else 2)
            self.assertNotIn('fake-key',json.dumps(state))
            self.assertEqual(len(state['metadata']['web_records']),0 if mode=='direct' else 1)
            if mode!='direct':
                self.assertEqual(state['metadata']['usage']['output_tokens'],6)
                second=Fixture.requests[1]
                if protocol=='chat':self.assertEqual(second['messages'][-1]['role'],'tool');self.assertEqual(second['messages'][-1]['tool_call_id'],'call_1')
                elif protocol=='anthropic':self.assertEqual(second['messages'][-1]['content'][0]['tool_use_id'],'call_1')
                else:self.assertEqual(second['input'][-1]['call_id'],'call_1');self.assertFalse(second['store'])
            if mode=='image':
                self.assertEqual(len(state['metadata']['web_images']),1)
                image=state['metadata']['web_images'][0];self.assertEqual(pathlib.Path(image['path']).read_bytes(),PNG)
                self.assertEqual(image['sha256'],hashlib.sha256(PNG).hexdigest())
                self.assertNotIn(base64.b64encode(PNG).decode(),json.dumps(state))
    def test_protocols(self):
        for protocol in ('responses','chat','anthropic'):
            for stream in (True,False):
                with self.subTest(protocol=protocol,stream=stream):self.run_case(protocol,stream)
    def test_direct_no_search(self):self.run_case('chat',mode='direct')
    def test_text_model_inline_image(self):self.run_case('responses',mode='image')
    def test_tool_syntax_is_ordinary_text(self):
        for protocol in ('responses','chat','anthropic'):
            for stream in (True,False):
                with self.subTest(protocol=protocol,stream=stream):self.run_case(protocol,stream,mode='syntax')
if __name__=='__main__':unittest.main()
