"""Physical-touch Messages acceptance against an isolated local fake server."""
import http.server,json,re,shutil,socket,sqlite3,subprocess,sys,threading,time

def prepare(root,folder,data,serial):
 sys.path.insert(0,str(root/'tests'))
 from test_image_viewer import fixture
 from anthropic_fixture import Handler,configure
 route=subprocess.check_output(['adb','-s',serial,'shell','ip -4 route show default'],text=True)
 ip=re.search(r'\bsrc (\d+\.\d+\.\d+\.\d+)',route).group(1)
 connection=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);connection.connect((ip,9));host=connection.getsockname()[0];connection.close()
 Handler.requests=[];Handler.mode='ok';Handler.delay=0
 server=http.server.ThreadingHTTPServer((host,0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
 if folder.exists():shutil.rmtree(folder)
 folder.mkdir();fixture(folder,port=server.server_port);configure(folder,server.server_port,host)
 with sqlite3.connect(folder/'penchat.db') as db:
  db.execute('update message_assets set stored_path=?,original_path=?',(data+'/assets/original.png',)*2)
 return server,Handler,[(folder/'penchat.db',data+'/penchat.db'),(folder/'assets/original.png',data+'/assets/original.png')]

def run(ctx,handler):
 from settings_ui import locate
 from anthropic_fixture import SIGNATURE
 state,tap,wait,capture=map(ctx.get,('state','tap','wait','capture'));W,H=ctx['W'],ctx['H']
 def click(b):tap((b[0]+b[2])//2,(b[1]+b[3])//2)
 def type_question():
  ctx['open_chat_editor']()
  if state()['editor']['chinese']:ctx['key']('language')
  ctx['key']('q');click(state()['editor']['actions']['done']);wait(lambda:not state()['editor']['active'])
 def settle(count):wait(lambda:len(handler.requests)==count and not state()['generating'] and not state()['compressing'] and not state()['storage']['unsaved'],timeout=25)
 def attachment(key):
  ctx['open_attachment_menu']()
  for _ in range(25):
   s=state();b=s['attachment_actions'][key];v=s['attachment_viewport']
   if b[1]>=v[1] and b[3]<=v[3]:click(b);return
   ctx['drag'](220,v[3]-10,220,max(v[1]+10,v[3]-110))
  raise AssertionError(key)
 def row(key):
  for _ in range(30):
   s=state();b=s['settings']['rows'][key]['bounds']
   if b[1]>=(s['safe_insets'][1] if H>W else 0) and b[3]<H:click(b);return
   ctx['settings_swipe'](max(-110,min(110,H//2-(b[1]+b[3])//2)))
  raise AssertionError(key)
 type_question();ctx['send_chat']();settle(1)
 assert not state()['modal'] and state()['reasoning_header_count']
 body=handler.requests[-1][2];assert body['thinking']['type']=='adaptive' and body['output_config']['effort']=='high'
 assert any(b.get('type')=='image' for m in body['messages'] for b in m['content'])
 assert handler.requests[-1][1]['x-api-key']=='fake-anthropic-key';capture('anthropic-chat')
 assert SIGNATURE not in json.dumps(state())
 type_question();ctx['send_chat']();settle(2)
 assert SIGNATURE in json.dumps(handler.requests[-1][2]);capture('anthropic-replay')
 attachment('session_info');wait(lambda:not state()['session_info_busy'])
 info=state()['session_info'];assert info['latest']['input_tokens']==20 and info['latest']['cached_tokens']==7
 assert info['latest']['uncached_input_tokens']==10 and info['totals']['total_tokens']==58
 capture('anthropic-information');ctx['settings_swipe'](-130);ctx['settings_swipe'](-130);capture('anthropic-usage');ctx['navigate_back_or_menu']()
 attachment('compress');wait(lambda:state()['view']=='context_summary' and not state()['compressing'],timeout=25);assert not state()['modal']
 assert 'PenChat_CONTEXT_COMPRESSION_V1' in handler.requests[-1][2]['system'] and 'thinking' not in handler.requests[-1][2]
 capture('anthropic-compression');ctx['navigate_back_or_menu']()
 count=len(handler.requests);type_question();ctx['send_chat']();settle(count+1)
 assert SIGNATURE not in json.dumps(handler.requests[-1][2]);capture('anthropic-after-compression')
 ctx['open_settings']();click(locate(state,ctx['settings_swipe'],'models')['bounds'])
 ctx['page_action_prefix']('provider-open:');ctx['page_action_prefix']('provider-protocol');assert state()['modal'];capture('anthropic-protocol-chooser');ctx['context_choice']('anthropic')
 row('provider_advanced');time.sleep(.35)
 row('anthropic_connection');capture('anthropic-connection');row('anthropic_auth');row('anthropic_version')
 capture('anthropic-bearer-version');ctx['navigate_back_or_menu']();row('api_compatibility');row('enabled')
 assert 'usage' not in state()['settings']['rows'];row('http1');capture('anthropic-compatible')
 ctx['navigate_back_or_menu']()
 ctx['page_action_prefix']('保存');wait(lambda:not state()['storage']['unsaved'])
 # Whole-page Save intentionally keeps the provider detail open. Leave it
 # explicitly before reopening the persisted workspace from its list.
 ctx['navigate_back_or_menu']();wait(lambda:not state()['provider_protocol'])
 # Capture both model modes without changing their configuration.
 ctx['page_action_prefix']('provider-open:');ctx['page_action_prefix']('model-edit:',0);assert state()['view']=='model';capture('anthropic-adaptive-model');ctx['navigate_back_or_menu']()
 ctx['page_action_prefix']('model-edit:',1);assert state()['view']=='model';capture('anthropic-budget-model');ctx['navigate_back_or_menu']()
 for _ in range(6):
  if state()['view']=='chat':break
  ctx['navigate_back_or_menu']()
 assert state()['view']=='chat'
 if state()['navigation_open']:tap(W-30,80)
 count=len(handler.requests);type_question();ctx['send_chat']();settle(count+1)
 headers=handler.requests[-1][1]
 assert headers['Authorization']=='Bearer fake-anthropic-key' and 'x-api-key' not in headers and headers['anthropic-version']=='2026-10-21'
 assert SIGNATURE not in json.dumps(handler.requests[-1][2])
 handler.mode='401';count=len(handler.requests);type_question();ctx['send_chat']();settle(count+1);assert state()['modal']
 capture('anthropic-http-error');time.sleep(.3);assert len(handler.requests)==count+1
 handler.mode='json';ctx['modal_action'](1,W-190);settle(count+2);assert not state()['modal'] and not state()['reply_actions']['confirm'];capture('anthropic-json-retry')
 ctx['result']['anthropic']={'local_fake_server':True,'image':True,'streaming':True,'signed_replay':True,
  'usage':True,'compression_archive':True,'auth_headers':['x-api-key','bearer'],'versions':['2023-06-01','2026-10-21'],
  'http1':True,'compatible_json':True,'manual_retry':True,'no_automatic_retry':True,'requests':len(handler.requests)}
