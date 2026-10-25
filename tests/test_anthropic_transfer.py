#!/usr/bin/env python3
"""Messages portable chat/preset roundtrip never exports signed state or credentials."""
import json,os,pathlib,sqlite3,tempfile,subprocess,zipfile,shutil
from anthropic_fixture import seed,SIGNATURE,REDACTED
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_TRANSFER_TEST_BINARY',ROOT/'build/host/penchat-transfer-tests'))
def run():
 with tempfile.TemporaryDirectory(prefix='penchat-anthropic-transfer-') as folder:
  root=pathlib.Path(folder);data=root/'data';data.mkdir();seed(data,1)
  prefs={'model_id':'a','effort':'high','system_prompt':'SYSTEM','compression_model_id':'b','has_sent':True}
  preset=dict(id='default',name='默认预设',model_id='a',model_when='always',prompt_mode='custom',prompt='SYSTEM',effort='high',effort_when='always',compression_model_id='b')
  metadata={'reasoning_observed':True,'reasoning_duration_ms':1234,'usage':{'input_tokens':20,'uncached_input_tokens':10,'cached_tokens':7,'cache_write_tokens':3,'output_tokens':9,'total_tokens':29},
            'anthropic_content':[{'type':'thinking','thinking':'public','signature':SIGNATURE},{'type':'redacted_thinking','data':REDACTED}]}
  with sqlite3.connect(data/'penchat.db') as db:
   db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at,default_model_config_id,default_provider_id,metadata_json) values('s','Messages export','active',1,1,1,'a','p',?)",(json.dumps({'chat_preferences':prefs}),))
   for id,seq,role,text in [('u',1,'user','question'),('a1',2,'assistant','**回答** $x^2$')]:
    db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,reasoning_text,provider_id,model_config_id,metadata_json,created_at,updated_at) values(?,'s',?,?,'complete','text',?,'公开思考','p','a',?,1,1)",(id,seq,role,text,json.dumps(metadata) if role=='assistant' else '{}'))
   db.execute("insert or replace into app_settings values('chat_presets',?,1)",(json.dumps([preset]),))
  public=root/'public';p=subprocess.Popen([str(BINARY),str(data),str(public)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
  def call(op,args={}):
   p.stdin.write(json.dumps({'op':op,'args':args})+'\n');p.stdin.flush();r=json.loads(p.stdout.readline())
   assert r['type']=='transfer_result',r
   return r['data'].get('data',r['data'])
  try:
   call('init');archive=pathlib.Path(call('export',{'kind':'chats','all':True})['path'])
   with zipfile.ZipFile(archive) as z:
    payload=b'\n'.join(z.read(name) for name in z.namelist())
    assert SIGNATURE.encode() not in payload and REDACTED.encode() not in payload and b'fake-anthropic-key' not in payload
    conversations=[json.loads(z.read(name)) for name in z.namelist() if name.startswith('conversations/')]
    assert conversations[0]['model']['api']=='anthropic'
   shutil.copy(archive,public/'导入/聊天'/archive.name)
   preview=call('preview',{'kind':'chats','name':archive.name})
   assert preview['models'] and all(m['id'] for m in preview['models'])
   call('import',{'indices':[0],'copy':True})
   pres=pathlib.Path(call('export',{'kind':'presets','all':True})['path'])
   exported=json.loads(pres.read_text());assert exported['items'][0]['model']['api']=='anthropic'
   shutil.copy(pres,public/'导入/预设'/pres.name);call('preview',{'kind':'presets','name':pres.name})
   call('import',{'indices':[0],'default_action':'copy','copy':True})
   with sqlite3.connect(data/'penchat.db') as db:
    assert db.execute('select count(*) from chat_sessions').fetchone()[0]==2
    messages=db.execute("select metadata_json,reasoning_text from chat_messages where session_id!='s' and role='assistant'").fetchall()
    assert messages[0][1]=='公开思考' and 'anthropic_content' not in json.loads(messages[0][0])
    saved=json.loads(db.execute("select value_json from app_settings where key='chat_presets'").fetchone()[0]);assert saved[-1]['model_id']=='a' and saved[-1]['compression_model_id']=='b'
  finally:p.stdin.close();assert p.wait(timeout=15)==0
 print('Messages chat/preset export/import: exact local model match, public thinking/usage retained, signatures/credentials excluded passed.')
if __name__=='__main__':run()
