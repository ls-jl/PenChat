#!/usr/bin/env python3
"""Native curl Messages conformance and explicit compatibility; one POST per operation."""
import http.server,json,os,pathlib,subprocess,threading
from anthropic_fixture import Handler,SIGNATURE,REDACTED
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_NETWORK_PROBE',ROOT/'build/host/penchat-network-probe'))
def run_suite():
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
  threading.Thread(target=server.serve_forever,daemon=True).start();results=[]
  def run(mode='ok',compat=False,config=None,task='',model_patch=None,inputs=None,cancel=0):
   Handler.mode=mode;before=len(Handler.requests)
   model={'name':'fixture','provider_id':'p','id':'m','api_key':'fake-anthropic-key','base_url':f'http://127.0.0.1:{server.server_port}/v1/messages',
          'provider_metadata':{'api':'anthropic','compatibility':dict(enabled=compat,**(config or {})),'anthropic':{'auth':'x-api-key','version':'2023-06-01'}},
          'headers_json':{'X-Fixture':'yes'},'system_prompt':'CHAT-ONLY','effort_value':'high','effective_effort':'high','reasoning_summary':True,
          'capabilities_json':{'reasoning':{'mode':'adaptive','levels':{'high':'high'},'summary':True,'disable':True,'display_supported':True},'max_output_tokens':16384}}
   if model_patch:model.update(model_patch)
   payload={'model':model,'task':task,'cancel_ms':cancel}
   if inputs is not None:payload['input']=inputs
   p=subprocess.run([str(BINARY)],input=json.dumps(payload),text=True,capture_output=True,timeout=12)
   assert p.returncode==0,p.stderr
   events=json.loads(p.stdout);end=events[-1];state=next((e['data'] for e in events if e['type']=='response_state'),{})
   requests=Handler.requests[before:]
   if task=='models_pages':assert len(requests)==2,requests
   else:assert len(requests)==1,(mode,end,requests)
   for _,headers,_ in requests:
    c=model['provider_metadata'].get('anthropic',{})
    if c.get('auth','x-api-key')=='bearer':assert headers.get('Authorization')=='Bearer fake-anthropic-key' and not headers.get('x-api-key')
    else:assert headers.get('x-api-key')=='fake-anthropic-key' and not headers.get('Authorization')
    assert headers['anthropic-version']==c.get('version','2023-06-01')
   if end['type']=='network_error':assert 'fake-anthropic-key' not in end['data']['error']
   results.append((mode,compat,task,end['type']))
   return end,state,requests[-1][2]
  for mode in ('ok','bytes','wrongtype','unknown','hidden','redacted','plain','refusal','stop','final'):
   end,state,body=run(mode);assert end['type']=='done',(mode,end)
   assert state['output']==('无法回答这个问题。' if mode=='refusal' else '中文回答。')
   assert state['reasoning']==('公开思考' if mode not in ('hidden','redacted','plain') else '')
   assert state['metadata']['usage']['input_tokens']==20 and state['metadata']['usage']['total_tokens']==29
   assert state['metadata']['usage']['cached_tokens']==7 and state['metadata']['usage']['cache_write_tokens']==3
   assert body['thinking']=={'type':'adaptive','display':'summarized'} and body['output_config']=={'effort':'high'}
   assert not any(k in body for k in ('input','store','include','reasoning_effort','stream_options','cache_control'))
   assert state['metadata']['http']['request_id']=='messages-fixture'
  for mode in ('length','context','tool','toolblock','pause','unknownstop','cut','invalid','sse_error','chat','large','html','401','429','500'):
   end,state,_=run(mode);assert end['type']=='network_error',(mode,end)
   assert 'anthropic_content' not in state.get('metadata',{})
  end,_,_=run('cancel',cancel=50);assert end['type']=='cancelled'
  end,_,_=run('missing_stop');assert end['type']=='network_error'
  end,state,_=run('missing_stop',True);assert end['type']=='done' and state['metadata']['anthropic_content']
  end,_,_=run('unclosed',True);assert end['type']=='network_error'
  for mode in ('json','wrongjson'):
   end,_,_=run(mode);assert end['type']=='network_error'
   end,state,_=run(mode,True);assert end['type']=='done' and state['output']=='中文回答。'
  end,state,body=run('json',True,{'streaming':False,'summary':False,'replay':False,'http1':True})
  assert end['type']=='done' and not body['stream'] and body['thinking']['display']=='omitted'
  assert 'anthropic_content' not in state['metadata']
  end,state,_=run('lateusage');assert end['type']=='done' and state['metadata']['usage']['output_tokens']==13
  end,state,_=run('length');assert state['metadata']['usage']['output_tokens']==9
  for mode in ('badcontent','empty','deep'):
   end,_,_=run(mode,True);assert end['type']=='network_error'
  end,state,body=run(model_patch={'effort_value':'','effective_effort':'api_default'})
  assert end['type']=='done' and 'thinking' not in body and 'output_config' not in body
  budget={'reasoning':{'mode':'budget','levels':{'medium':'4096','high':'8192'},'summary':True,'disable':True},'max_output_tokens':16384}
  end,_,body=run(model_patch={'effort_value':'8192','capabilities_json':budget})
  assert end['type']=='done' and body['thinking']=={'type':'enabled','budget_tokens':8192} and 'output_config' not in body
  end,_,body=run(model_patch={'effort_value':'disabled','effective_effort':'none'})
  assert end['type']=='done' and body['thinking']=={'type':'disabled'}
  img='data:image/png;base64,iVBORw0KGgo='
  inputs=[{'role':'system','content':[{'type':'input_text','text':'TASK-SYSTEM'}]},
          {'role':'user','content':[{'type':'input_text','text':'picture'},{'type':'input_image','image_url':img}]}]
  end,_,body=run(inputs=inputs);assert end['type']=='done' and body['system']=='CHAT-ONLY\n\nTASK-SYSTEM'
  assert body['messages'][0]['content'][1]['source']=={'type':'base64','media_type':'image/png','data':'iVBORw0KGgo='}
  replay=[{'role':'assistant','content':[{'type':'thinking','thinking':'public','signature':SIGNATURE}]},{'role':'user','content':'next'}]
  end,_,_=run('401',inputs=replay);assert SIGNATURE not in end['data']['error']
  end,_,_=run(task='title');assert end['type']=='title' and end['data']['text']=='Messages 标题'
  body=Handler.requests[-1][2];assert 'CHAT-ONLY' not in json.dumps(body) and 'thinking' not in body
  end,_,_=run(model_patch={'provider_metadata':{'api':'anthropic','anthropic':{'auth':'bearer','version':'2026-10-21'}}});assert end['type']=='done'
  end,_,_=run(task='models_pages');assert end['type']=='models_list' and len(end['data']['body']['data'])==2
  print('Anthropic Messages:',len(results),'native protocol/compatibility cases passed; one request per operation (model pagination excepted).')
if __name__=='__main__':run_suite()
