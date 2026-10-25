#!/usr/bin/env python3
"""Real LVGL Messages chat/settings, opaque persistence, retry, branch and compaction."""
import http.server,json,os,pathlib,signal,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from anthropic_fixture import Handler,seed,configure,SIGNATURE,REDACTED
from test_reply_actions import action as reply_action,confirm as confirm_retry,bottom
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/anthropic-ui';OUT.mkdir(parents=True,exist_ok=True)
def shot(a,name):
 from image_gestures import capture
 capture(a,OUT/(name+'.png'))
def locate(a,name):
 for _ in range(30):
  s=a.diag();b=s['settings']['rows'][name]['bounds']
  if b[1]>=0 and b[3]<s['height']:
   time.sleep(.12);return a.diag()['settings']['rows'][name]['bounds']
  a.settings_swipe(max(-110,min(110,s['height']//2-(b[1]+b[3])//2)));time.sleep(.2)
 raise AssertionError(name)
def click(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def row(a,name):click(a,locate(a,name))
def enter(a,value):
 r=a.receive('keyboard.open');a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text=value)
 t.wait_for(lambda:not a.diag()['editor']['active'])
def settled(a):t.wait_for(lambda:not a.diag()['generating'] and not a.diag()['compressing'] and not a.diag()['storage']['unsaved'],timeout=25)
def configuration(w,h):
 with tempfile.TemporaryDirectory(prefix='penchat-anthropic-settings-') as folder:
  p=pathlib.Path(folder);seed(p,1);a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
  try:
   a.open_settings();a.setting('models');a.tap(w-90,76)
   a.tap(w-230,128);assert a.diag()['modal'];shot(a,f'{w}-protocols')
   a.tap(400,112);assert a.diag()['provider_protocol']=='chat'
   a.tap(w-230,128);a.tap(400,160);assert a.diag()['provider_protocol']=='anthropic'
   row(a,'anthropic_connection');assert a.diag()['view']=='anthropic_connection';shot(a,f'{w}-connection-default')
   row(a,'anthropic_auth');row(a,'anthropic_version');row(a,'anthropic_headers')
   enter(a,json.dumps({'X-Fixture':'yes','anthropic-workspace-id':'workspace-test'}))
   shot(a,f'{w}-connection-bearer')
   with sqlite3.connect(p/'penchat.db') as db:assert json.loads(db.execute('select metadata_json from providers').fetchone()[0])['anthropic']['auth']=='x-api-key'
   a.tap(40,32);row(a,'api_compatibility');row(a,'enabled')
   assert 'usage' not in a.diag()['settings']['rows'];shot(a,f'{w}-compatibility')
   a.tap(40,32);a.page_action('保存')
   with sqlite3.connect(p/'penchat.db') as db:
    base,metadata,headers=db.execute('select base_url,metadata_json,headers_json from providers').fetchone()
    assert base=='http://127.0.0.1:1/v1'
    assert json.loads(metadata)['anthropic']==dict(auth='bearer',version='2026-10-21')
    assert json.loads(headers)['anthropic-workspace-id']=='workspace-test'
    assert db.execute("select count(*) from model_configs where id in ('a','b')").fetchone()[0]==2
   # The first model remains the same row; unknown service capabilities are editable.
   a.tap(w-90,125);assert a.diag()['view']=='model';shot(a,f'{w}-adaptive-model')
   row(a,'max_output');enter(a,'32768');a.page_action('参数映射')
   row(a,'thinking_high');enter(a,'xhigh');shot(a,f'{w}-adaptive-mapping')
   a.page_action('保存');assert a.diag()['view']=='provider'
   a.tap(w-90,178);assert a.diag()['view']=='model';shot(a,f'{w}-budget-model')
   a.page_action('参数映射');row(a,'thinking_high');enter(a,'20000')
   a.page_action('保存');assert a.diag()['modal'];shot(a,f'{w}-invalid-budget')
   a.tap(w-70,h-44);row(a,'thinking_high');enter(a,'8192');a.page_action('保存')
   assert a.diag()['view']=='provider'
  finally:a.close()

def chat(w,h):
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server,tempfile.TemporaryDirectory(prefix='penchat-anthropic-chat-') as folder:
  threading.Thread(target=server.serve_forever,daemon=True).start();Handler.requests=[];Handler.mode='ok';Handler.delay=0
  p=pathlib.Path(folder)
  from test_image_viewer import fixture as photo_fixture
  photo_fixture(p,port=server.server_port);configure(p,server.server_port)
  with sqlite3.connect(p/'penchat.db') as db:
   prefs={'model_id':'a','effort':'high','system_prompt':'CHAT_SYSTEM_ONCE','has_sent':True}
   db.execute("update chat_sessions set default_model_config_id='a',default_provider_id='p',metadata_json=? where id='fixture'",(json.dumps({'chat_preferences':prefs}),))
   db.execute("insert or replace into app_settings values('chat_presets',?,1)",(json.dumps([dict(id='expert',name='Claude preset',prompt_mode='custom',prompt='PRESET_SYSTEM',model_id='b',model_when='always',effort='high',effort_when='always',compression_model_id='b')]),))
  a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
  try:
   a.edit('第一条 Messages 问题');a.tap(w-52,h-34);settled(a)
   assert len(Handler.requests)==1 and not a.diag()['modal'],a.read_messages()
   request=Handler.requests[-1][2];assert request['system']=='CHAT_SYSTEM_ONCE'
   assert any(b.get('type')=='image' for m in request['messages'] for b in m['content'])
   assert a.diag()['reasoning_header_count']>=1;shot(a,f'{w}-reply-and-thinking')
   with sqlite3.connect(p/'penchat.db') as db:
    mid,metadata=db.execute("select id,metadata_json from chat_messages where role='assistant' order by seq desc limit 1").fetchone()
    meta=json.loads(metadata);assert meta['anthropic_content'][0]['signature']==SIGNATURE
    assert meta['usage']['input_tokens']==20 and meta['usage']['uncached_input_tokens']==10
   assert SIGNATURE not in (p/'runtime.log').read_text() and SIGNATURE not in json.dumps(a.diag())
   a.edit('继续');a.tap(w-52,h-34);settled(a);assert len(Handler.requests)==2
   assert any(b.get('signature')==SIGNATURE for m in Handler.requests[-1][2]['messages'] for b in m['content'])
   a.attachment_action('session_info');t.wait_for(lambda:not a.diag()['session_info_busy'])
   s=a.diag()['session_info'];assert s['latest']['total_tokens']==29 and s['totals']['total_tokens']==58
   assert s['latest']['uncached_input_tokens']==10 and s['cache_read_tokens']==14
   shot(a,f'{w}-information');a.settings_swipe(-130);a.settings_swipe(-130);shot(a,f'{w}-usage');a.tap(40,32)
   a.edit('未发送草稿');before=a.diag()['draft_length'];reply_action(a,mid,'branch')
   t.wait_for(lambda:not a.diag()['reply_actions']['busy']);assert a.diag()['draft_length']==before
   branch=a.diag()['session'];assert branch!='fixture' and len(Handler.requests)==2
   shot(a,f'{w}-branch');a.edit('')
   with sqlite3.connect(p/'penchat.db') as db:
    pivot=db.execute("select id from chat_messages where session_id=? and role='assistant' order by seq desc limit 1",(branch,)).fetchone()[0]
   reply_action(a,pivot,'retry')
   assert a.diag()['reply_actions']['confirm'];confirm_retry(a,True);settled(a)
   assert len(Handler.requests)==3 and a.diag()['session']==branch and not a.diag()['modal']
   assert '未发送草稿' not in json.dumps(Handler.requests[-1][2],ensure_ascii=False)
   # Manual compression goes through Messages with its own instruction, no effort.
   a.attachment_action('compress');t.wait_for(lambda:not a.diag()['compressing'],timeout=25)
   shot(a,f'{w}-compression-result')
   with sqlite3.connect(p/'penchat.db') as db:
    checkpoint=json.loads(db.execute('select metadata_json from chat_sessions where id=?',(branch,)).fetchone()[0])['context_compression']
    assert checkpoint['archive_id'] and checkpoint['summary']
   assert a.diag()['modal'];a.tap(w-70,h-44)
   compressed=Handler.requests[-1][2]
   assert 'PenChat_CONTEXT_COMPRESSION_V1' in compressed.get('system','') and 'CHAT_SYSTEM_ONCE' not in compressed['system']
   assert 'thinking' not in compressed and 'output_config' not in compressed
   a.attachment_action('summary');shot(a,f'{w}-summary');a.tap(w-70,h-44)
   a.edit('压缩后继续');a.tap(w-52,h-34);settled(a)
   body=Handler.requests[-1][2];assert SIGNATURE not in json.dumps(body)
   assert '历史参考摘要' in json.dumps(body,ensure_ascii=False)
   # Unknown-outcome failures do not cause a second HTTP request.
   Handler.mode='401';a.edit('鉴权错误');before=len(Handler.requests);a.tap(w-52,h-34);settled(a)
   assert len(Handler.requests)==before+1 and a.diag()['modal'];shot(a,f'{w}-http-error')
   time.sleep(.3);assert len(Handler.requests)==before+1
  finally:a.close();Handler.delay=0
 print(f'Messages LVGL {w}x{h}: images, public/opaque reasoning, usage, retry/branch, compression and failure passed.',flush=True)

def streaming():
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server,tempfile.TemporaryDirectory(prefix='penchat-anthropic-stream-') as folder:
  threading.Thread(target=server.serve_forever,daemon=True).start();Handler.requests=[];Handler.mode='ok';Handler.delay=.15
  p=pathlib.Path(folder);seed(p,server.server_port);a=t.Native(BINARY,p)
  try:
   a.edit('stream question');a.tap(908,232)
   t.wait_for(lambda:a.diag()['generating'] and a.diag()['reasoning_observed']);shot(a,'stream-thinking')
   a.attachment_action('models');a.tap(300,114);assert a.diag()['active_model']=='b'
   a.edit('UNSENT_STREAM_DRAFT');settled(a)
   assert len(Handler.requests)==1 and Handler.requests[0][2]['model']=='a'
   assert Handler.requests[0][2]['thinking']['type']=='adaptive' and a.diag()['draft_length']==19
   with sqlite3.connect(p/'penchat.db') as db:
    meta=json.loads(db.execute("select metadata_json from chat_messages where role='assistant' order by seq desc limit 1").fetchone()[0])
    assert 0<meta['reasoning_duration_ms']<meta['http']['elapsed_ms'] and not meta['reasoning_estimated']
   a.tap(908,232);settled(a)
   assert Handler.requests[-1][2]['model']=='b' and Handler.requests[-1][2]['thinking']=={'type':'enabled','budget_tokens':8192}
   assert 'UNSENT_STREAM_DRAFT' in json.dumps(Handler.requests[-1][2])
   a.edit('cancel');a.tap(908,232);t.wait_for(lambda:a.diag()['generating']);a.tap(908,232);settled(a)
   assert len(Handler.requests)==3
   with sqlite3.connect(p/'penchat.db') as db:
    status,meta=db.execute("select status,metadata_json from chat_messages where role='assistant' order by seq desc limit 1").fetchone()
    assert status=='cancelled' and 'anthropic_content' not in json.loads(meta)
   Handler.delay=0;Handler.mode='hidden';a.edit('hidden thinking');a.tap(908,232);settled(a)
   assert a.diag()['reasoning_observed'];shot(a,'hidden-thinking-timing')
   with sqlite3.connect(p/'penchat.db') as db:
    reason,meta=db.execute("select reasoning_text,metadata_json from chat_messages where role='assistant' order by seq desc limit 1").fetchone()
    assert not reason and json.loads(meta)['reasoning_observed']
   assert SIGNATURE not in (p/'runtime.log').read_text() and REDACTED not in json.dumps(a.diag())
   a.open_settings();a.setting('models');a.tap(870,76)
   a.tap(250,180);enter(a,'');before=len(Handler.requests)
   a.tap(250,235);t.wait_for(lambda:len(Handler.requests)==before+2);time.sleep(.25)
   a.page_action('保存')
   with sqlite3.connect(p/'penchat.db') as db:
    names=[n for n, in db.execute('select name from model_configs where enabled=1 order by name')]
    assert names==['claude-fixture','claude-fixture-second']
  finally:a.close();Handler.delay=0
 print('Messages slow stream: live thinking/timing, frozen model, retained draft, budget next request, cancellation and hidden timing passed.',flush=True)

def automatic_title():
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server,tempfile.TemporaryDirectory(prefix='penchat-anthropic-title-') as folder:
  threading.Thread(target=server.serve_forever,daemon=True).start();Handler.requests=[];Handler.mode='plain';Handler.delay=0
  p=pathlib.Path(folder);seed(p,server.server_port)
  with sqlite3.connect(p/'penchat.db') as db:
   db.execute("insert or replace into app_settings values('auto_title_enabled','true',1)")
   preset={'id':'default','name':'默认预设','prompt_mode':'custom','prompt':'CHAT_ONLY','model_id':'a','model_when':'empty'}
   db.execute("insert or replace into app_settings values('chat_presets',?,1)",(json.dumps([preset]),))
  a=t.Native(BINARY,p)
  try:
   a.edit('自动命名');a.tap(908,232);settled(a)
   t.wait_for(lambda:len(Handler.requests)==2 and not a.diag()['title_busy'] and not a.diag()['title_pending'])
   assert Handler.requests[0][2]['system']=='CHAT_ONLY'
   title=Handler.requests[1][2];assert '[penchat-title]' in title['system'] and 'CHAT_ONLY' not in json.dumps(title)
   assert 'thinking' not in title and 'output_config' not in title
   with sqlite3.connect(p/'penchat.db') as db:assert db.execute('select title from chat_sessions').fetchone()[0]=='Messages 标题'
   a.edit('第二个问题');a.tap(908,232);settled(a);time.sleep(.2);assert len(Handler.requests)==3
  finally:a.close()
 print('Messages automatic title: first final save, independent prompt, default effort and no repeat passed.',flush=True)

if __name__=='__main__':
 for w,h in ((960,266),(936,280)):configuration(w,h);chat(w,h)
 streaming();automatic_title()
 print('Messages settings: protocol chooser, headers/auth/version, explicit saves, model mappings and budget validation passed.')
