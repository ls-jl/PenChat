#!/usr/bin/env python3
"""Real SQLite/file incremental restoration; isolated data, no devices or API calls."""
import base64,hashlib,json,os,pathlib,shutil,sqlite3,subprocess,tempfile,time,unittest,uuid
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'.work/backup-restore';OUT.mkdir(exist_ok=True);(OUT/'tmp').mkdir(exist_ok=True)
BINARY=pathlib.Path(os.environ.get('PENCHAT_BACKUP_RESTORE_PROBE',ROOT/'build/host/penchat-backup-restore-probe'))
APP=json.loads((ROOT/'package.json').read_text())
PNG=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=')
def sql(path,text,args=()):
 with sqlite3.connect(path/'penchat.db') as db:return db.execute(text,args).fetchall()
def row(db,table,**values):
 keys=','.join(values);db.execute(f'insert into {table}({keys}) values({",".join("?" for _ in values)})',tuple(values.values()))
class Probe:
 def __init__(self,data,public,env=None):
  self.data=data;self.public=public;self.p=subprocess.Popen([str(BINARY),str(data),str(public)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,**(env or {})))
  ready=self.p.stdout.readline();assert ready and json.loads(ready).get('ready'),(ready,self.p.stderr.read())
 def cmd(self,op,**args):
  self.p.stdin.write(json.dumps(dict(op=op,**args))+'\n');self.p.stdin.flush();value=json.loads(self.p.stdout.readline());return value
 def wait(self):
  end=time.monotonic()+20
  while time.monotonic()<end:
   for event in self.cmd('events')['value']:
    if event['type']=='backup_error':return {'error':event['data']['message']}
    if event['type']=='backup_result':return event['data']
   time.sleep(.02)
  raise AssertionError('Backup operation timed out')
 def preview(self,id):
  assert self.cmd('start',kind='preview',args={'id':id})['value']['started'];return self.wait()
 def close(self):
  if self.p.poll() is None:
   self.p.stdin.write('{"op":"quit"}\n');self.p.stdin.flush();self.p.wait(timeout=10)
  self.p.stdin.close();self.p.stdout.close();self.p.stderr.close()
class Restore(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='case-',dir=OUT/'tmp');self.root=pathlib.Path(self.temp.name);self.src=self.root/'source';self.data=self.root/'current';self.pub=self.root/'public';self.srcpub=self.root/'source-public'
  for d in [self.src,self.data,self.pub,self.srcpub]:d.mkdir()
  for d in [self.src,self.data]:subprocess.run([str(BINARY),str(d),str(self.pub),'--init'],check=True,capture_output=True)
  self.seed(self.src,True);self.seed(self.data,False)
  (self.pub/'same.txt').write_text('current file');(self.srcpub/'same.txt').write_text('backup file');(self.srcpub/'new.txt').write_text('added file')
  self.id=str(uuid.uuid4());self.backup=self.data/'.rescue/backups'/self.id;self.pack();self.probe=None
 def tearDown(self):
  if self.probe:self.probe.close()
  self.temp.cleanup()
 def seed(self,path,source):
  (path/'assets').mkdir();(path/'assets/asset_1_1.png').write_bytes(PNG if source else PNG+b'CURRENT')
  (path/'ime').mkdir();(path/'ime/user.json').write_text(json.dumps({'version':1,'records':[{'pinyin':'hao','word':'好','count':2 if source else 8,'last':1}]}))
  with sqlite3.connect(path/'penchat.db') as db:
   row(db,'providers',id='p',name='Provider',type='openai-compatible',base_url='https://backup.invalid' if source else 'https://current.invalid',api_key='backup-secret' if source else 'current-secret',created_at=1,updated_at=1,is_default=1)
   row(db,'model_configs',id='m',provider_id='p',name='fixture',display_name='Model',model_type='chat',created_at=1,updated_at=1,is_default=1)
   presets=[{'id':'default','name':'默认预设','prompt_mode':'custom','prompt':'backup prompt' if source else 'current prompt'}]
   presets.append({'id':'source-only' if source else 'current-only','name':'Preset','prompt_mode':'inherit'})
   for key,value in [('theme_mode','light' if source else 'dark'),('ui_scale_percent',125 if source else 100),('chat_presets',presets),('active_session_id','source-new' if source else 'current-only')]:row(db,'app_settings',key=key,value_json=json.dumps(value),updated_at=1)
   for sid in ['conflict','source-new' if source else 'current-only']:
    row(db,'chat_sessions',id=sid,title=('Backup ' if source else 'Current ')+sid,status='active',created_at=1,updated_at=1,last_message_at=1,default_provider_id='p',default_model_config_id='m',metadata_json=json.dumps({'title_state':'pending','title_token':'old-token'}))
    mid='message-'+sid;row(db,'chat_messages',id=mid,session_id=sid,seq=1,role='user',status='complete',message_type='text',created_at=1,updated_at=1,output_text='backup text' if source else 'current text')
    row(db,'message_parts',id='part-'+sid,session_id=sid,message_id=mid,part_index=0,part_type='text',text='backup text' if source else 'current text')
   owner='conflict' if source else 'current-only';mid='message-'+owner
   row(db,'message_assets',id='shared-asset',session_id=owner,message_id=mid,kind='image',source_type='photo',stored_path=str(path/'assets/asset_1_1.png'),display_src=str(path/'assets/asset_1_1.png'),mime_type='image/png',width=1,height=1,created_at=1)
   row(db,'message_parts',id='image-'+owner,session_id=owner,message_id=mid,part_index=1,part_type='image',asset_id='shared-asset')
   row(db,'chat_drafts',draft_key='new',text='backup draft' if source else 'current draft')
   row(db,'chat_drafts',draft_key='conflict',text='backup chat draft' if source else 'current chat draft')
  if source:
   folder=path/'assets-drafts/image_1_1';folder.mkdir(parents=True);(folder/'path.png').write_bytes(PNG)
   sql(path,"insert into draft_attachments values('draft-a','conflict',0,?)",(json.dumps({'path':str(folder/'path.png'),'previewPath':str(folder/'path.png'),'source':'photo'}),))
 def pack(self,change=None):
  if self.backup.exists():shutil.rmtree(self.backup)
  self.backup.mkdir(parents=True)
  shutil.copytree(self.src,self.backup/'data',ignore=shutil.ignore_patterns('.rescue'));shutil.copytree(self.srcpub,self.backup/'public')
  files=[]
  for group in ['data','public']:
   for p in sorted((self.backup/group).rglob('*')):
    f={'path':p.relative_to(self.backup).as_posix()}
    if p.is_dir():f['directory']=True
    else:f.update(size=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
    files.append(f)
  m=dict(format=1,operation=self.id,time_ms=1,appid=APP['appid'],source_version=APP['version'],source_schema=3,data_root=str(self.src),files=files)
  if change:change(m)
  (self.backup/'manifest.json').write_text(json.dumps(m))
 def open(self,env=None):self.probe=Probe(self.data,self.pub,env);return self.probe
 def choices(self,plan,action='replace'):
  return dict(plan=plan['plan'],actions={u['key']:action for u in plan['items'] if u['conflict']})
 def test_add_new_skip_existing_and_preserve_current_assets(self):
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  result=p.cmd('commit',choices=self.choices(plan,'skip'));self.assertNotIn('error',result,result)
  self.assertEqual(sql(self.data,"select output_text from chat_messages where id='message-conflict'")[0][0],'current text')
  self.assertEqual(sql(self.data,"select title from chat_sessions where id='source-new'")[0][0],'Backup source-new')
  self.assertEqual(sql(self.data,"select title from chat_sessions where id='current-only'")[0][0],'Current current-only')
  self.assertEqual(sql(self.data,"select api_key from providers where id='p'")[0][0],'current-secret')
  self.assertEqual((self.pub/'same.txt').read_text(),'current file');self.assertEqual((self.pub/'new.txt').read_text(),'added file')
  self.assertEqual((self.data/'assets/asset_1_1.png').read_bytes(),PNG+b'CURRENT')
  self.assertEqual(json.loads(sql(self.data,"select value_json from app_settings where key='theme_mode'")[0][0]),'dark')
 def test_whole_replace_retains_unselected_chats_and_restores_drafts_images_configs(self):
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan);result=p.cmd('commit',choices=self.choices(plan));self.assertNotIn('error',result,result)
  self.assertEqual(sql(self.data,"select output_text from chat_messages where id='message-conflict'")[0][0],'backup text')
  self.assertEqual(sql(self.data,"select count(*) from chat_sessions where id='current-only'")[0][0],1)
  self.assertEqual(sql(self.data,"select api_key from providers where id='p'")[0][0],'backup-secret')
  self.assertEqual(sql(self.data,"select text from chat_drafts where draft_key='conflict'")[0][0],'backup chat draft')
  image=json.loads(sql(self.data,"select image_json from draft_attachments where draft_key='conflict'")[0][0]);self.assertEqual(pathlib.Path(image['path']).read_bytes(),PNG);self.assertNotEqual(image['path'],image['previewPath'])
  restored=sql(self.data,"select stored_path from message_assets where session_id='conflict'")[0][0];self.assertEqual(pathlib.Path(restored).read_bytes(),PNG)
  self.assertEqual((self.data/'assets/asset_1_1.png').read_bytes(),PNG+b'CURRENT')
  self.assertEqual(sql(self.data,"select value_json from app_settings where key='active_session_id'")[0][0],json.dumps('current-only'))
  self.assertEqual(json.loads(sql(self.data,"select metadata_json from chat_sessions where id='conflict'")[0][0])['title_state'],'manual')
  self.assertEqual((self.pub/'same.txt').read_text(),'backup file');self.assertEqual(json.loads((self.data/'ime/user.json').read_text())['records'][0]['count'],2)
 def test_same_provider_model_name_maps_to_existing_identity(self):
  sql(self.data,"update model_configs set id='m-current' where id='m'")
  sql(self.data,"update chat_sessions set default_model_config_id='m-current'")
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan);result=p.cmd('commit',choices=self.choices(plan));self.assertNotIn('error',result,result)
  self.assertEqual(sql(self.data,"select id from model_configs where provider_id='p' and name='fixture'"),[('m-current',)])
  self.assertEqual(sql(self.data,"select default_model_config_id from chat_sessions where id='source-new'"),[('m-current',)])
 def test_source_database_schema_relations_and_config_types_are_rejected(self):
  cases=["update schema_meta set value='999' where key='schema_version'","insert into model_configs(id,provider_id,name,display_name,model_type,created_at,updated_at) values('orphan','missing','m','m','chat',1,1)","update app_settings set value_json='123' where key='theme_mode'","create view unexpected as select * from providers", "insert into app_settings values('penchat_settings','{\"autoSendCapturedContent\":1}',1)"]
  original=(self.src/'penchat.db').read_bytes()
  for change in cases:
   (self.src/'penchat.db').write_bytes(original);sql(self.src,change);self.pack();p=self.open();self.assertIn('error',p.preview(self.id),change);p.close();self.probe=None
 def test_shared_metadata_images_are_cloned_without_touching_private_response_items(self):
  for path,source in [(self.src,True),(self.data,False)]:
   with sqlite3.connect(path/'penchat.db') as db:
    row(db,'chat_sessions',id='other',title='Other',status='active',created_at=1,updated_at=1,last_message_at=1)
    row(db,'chat_messages',id='m-other',session_id='other',seq=1,role='user',status='complete',message_type='text',created_at=1,updated_at=1,output_text='other')
    file=path/'assets/asset_2_2.png';file.write_bytes(PNG if source else PNG+b'KEEP')
    row(db,'message_assets',id='shared-ref',session_id='other',message_id='m-other',kind='image',source_type='photo',stored_path=str(file),created_at=1)
    row(db,'message_parts',id='p-other',session_id='other',message_id='m-other',part_index=0,part_type='image',asset_id='shared-ref')
  meta={'web_images':[{'asset_id':'shared-ref'}],'responses_output':{'asset_id':'shared-ref','signature':'SIGNED'}}
  sql(self.src,"update chat_messages set metadata_json=? where id='message-conflict'",(json.dumps(meta),));self.pack()
  p=self.open();plan=p.preview(self.id);choices=self.choices(plan)
  other=next(u for u in plan['items'] if u['kind']=='chat' and u['name']=='聊天 · Other');choices['actions'][other['key']]='skip'
  result=p.cmd('commit',choices=choices);self.assertNotIn('error',result,result)
  restored=json.loads(sql(self.data,"select metadata_json from chat_messages where id='message-conflict'")[0][0]);id=restored['web_images'][0]['asset_id'];self.assertNotEqual(id,'shared-ref');self.assertEqual(restored['responses_output'],meta['responses_output'])
  image=sql(self.data,"select stored_path from message_assets where id=?",(id,))[0][0];self.assertEqual(pathlib.Path(image).read_bytes(),PNG);self.assertEqual((self.data/'assets/asset_2_2.png').read_bytes(),PNG+b'KEEP')
 def test_replacing_chat_keeps_images_referenced_by_retained_chat_metadata(self):
  sql(self.data,"update message_assets set session_id='conflict',message_id='message-conflict' where id='shared-asset'")
  sql(self.data,"update chat_messages set metadata_json=? where id='message-current-only'",(json.dumps({'web_images':[{'asset_id':'shared-asset'}]}),))
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  self.assertNotIn('error',p.cmd('commit',choices=self.choices(plan)))
  self.assertEqual(sql(self.data,"select session_id,message_id from message_assets where id='shared-asset'"),[('current-only','message-current-only')])
  p.close();self.probe=None;self.open() # Startup GC must retain the old shared picture.
  self.assertEqual(sql(self.data,"select count(*) from message_assets where id='shared-asset'"),[(1,)])
  self.assertEqual((self.data/'assets/asset_1_1.png').read_bytes(),PNG+b'CURRENT')
 def test_reply_choices_keep_hidden_history_and_selectable_results(self):
  with sqlite3.connect(self.src/'penchat.db') as db:
   db.row_factory=sqlite3.Row
   for mid,seq,role,text in [('a',2,'assistant','second result'),('tail-u',3,'user','later question'),('tail-a',4,'assistant','later answer')]:
    row(db,'chat_messages',id=mid,session_id='conflict',seq=seq,role=role,status='complete',message_type='text',created_at=1,updated_at=1,output_text=text)
    row(db,'message_parts',id='p-'+mid,session_id='conflict',message_id=mid,part_index=0,part_type='text',text=text)
   rows=[]
   for m in db.execute("select * from chat_messages where session_id='conflict' order by seq").fetchall():
    r=dict(m);r['_parts']=[dict(p) for p in db.execute('select * from message_parts where message_id=?',(r['id'],))];r['_events']=[];rows.append(r)
   baseline={'session':dict(db.execute("select * from chat_sessions where id='conflict'").fetchone()),'rows':rows}
   original=dict(next(m for m in rows if m['id']=='a'));original['output_text']='first result';original['_parts']=[dict(original['_parts'][0],text='first result')]
   second=next(m for m in rows if m['id']=='a')
   row(db,'reply_groups',id='g',session_id='conflict',message_id='a',selected_id='r2',baseline_json=json.dumps(baseline),created_at=1)
   for id,ordinal,payload in [('r1',1,original),('r2',2,second)]:row(db,'reply_results',id=id,group_id='g',ordinal=ordinal,payload_json=json.dumps(payload))
   row(db,'reply_asset_refs',group_id='g',owner_id='g',asset_id='shared-asset')
  self.pack();p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan);result=p.cmd('commit',choices=self.choices(plan));self.assertNotIn('error',result,result)
  self.assertEqual(sql(self.data,"select count(*) from chat_messages where session_id='conflict'")[0][0],4)
  self.assertEqual(len(p.cmd('context',session='conflict')['value']['rows']),2)
  self.assertNotIn('error',p.cmd('choose',session='conflict',message='a',ordinal=1));self.assertEqual(len(p.cmd('context',session='conflict')['value']['rows']),4)
  self.assertEqual(sql(self.data,"select output_text from chat_messages where id='a'")[0][0],'first result')
 def test_manifest_hash_extra_paths_symlink_and_wrong_app_are_rejected(self):
  for kind in ['hash','extra','duplicate','escape','symlink','hardlink','appid']:
   self.pack()
   if kind=='hash':(self.backup/'public/same.txt').write_text('tampered')
   elif kind=='extra':(self.backup/'unlisted').write_text('bad')
   elif kind=='hardlink':
    (self.backup/'public/same.txt').unlink();os.link(self.pub/'same.txt',self.backup/'public/same.txt')
   elif kind=='symlink':(self.backup/'public/same.txt').unlink();(self.backup/'public/same.txt').symlink_to(self.pub/'same.txt')
   else:
    m=json.loads((self.backup/'manifest.json').read_text())
    if kind=='duplicate':m['files'].append(m['files'][0])
    elif kind=='escape':m['files'].append(dict(path='data/../../escape',size=0,sha256=hashlib.sha256(b'').hexdigest()))
    elif kind=='appid':m['appid']='other-app'
    (self.backup/'manifest.json').write_text(json.dumps(m))
   p=self.open();self.assertIn('error',p.preview(self.id),kind);p.close();self.probe=None
   self.assertEqual((self.pub/'same.txt').read_text(),'current file')
 def test_stale_source_current_or_missing_decisions_never_overwrite(self):
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  self.assertIn('error',p.cmd('commit',choices=dict(plan=plan['plan'],actions={})))
  p.close();self.probe=None;p=self.open();plan=p.preview(self.id)
  p.cmd('q',sql="update chat_sessions set title='Newer title' where id='conflict'")
  self.assertIn('error',p.cmd('commit',choices=self.choices(plan)));self.assertEqual(sql(self.data,"select title from chat_sessions where id='conflict'")[0][0],'Newer title')
 def test_committed_wal_is_replayed_without_trusting_shared_memory(self):
  db=sqlite3.connect(self.src/'penchat.db')
  try:
   db.execute('pragma journal_mode=wal');db.execute('pragma wal_autocheckpoint=0')
   db.execute("update chat_sessions set title='Committed WAL' where id='source-new'");db.commit()
   self.assertGreater((self.src/'penchat.db-wal').stat().st_size,32);self.pack()
  finally:db.close()
  # SHM is architecture-specific and rebuilt in the private staging copy.
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  self.assertNotIn('error',p.cmd('commit',choices=self.choices(plan)))
  self.assertEqual(sql(self.data,"select title from chat_sessions where id='source-new'"),[('Committed WAL',)])
 def test_hot_rollback_journal_recovers_prior_committed_state(self):
  code="""import sqlite3,sys,os
p=sys.argv[1];db=sqlite3.connect(p)
db.execute('pragma journal_mode=delete');db.execute('pragma cache_size=1');db.execute('begin immediate')
db.execute("update chat_sessions set title='Uncommitted' where id='source-new'")
for i in range(80):db.execute('insert into app_settings values(?,?,?)',('temporary-'+str(i),'"'+'x'*4096+'"',1))
os._exit(77)
"""
  self.assertEqual(subprocess.run(['python3','-c',code,str(self.src/'penchat.db')]).returncode,77)
  self.assertTrue((self.src/'penchat.db-journal').exists());self.pack()
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  self.assertNotIn('error',p.cmd('commit',choices=self.choices(plan)))
  self.assertEqual(sql(self.data,"select title from chat_sessions where id='source-new'"),[('Backup source-new',)])
 def test_cancelled_plan_and_changed_backup_are_rejected(self):
  p=self.open();plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  p.cmd('cancel');self.assertNotIn('error',p.wait())
  self.assertIn('error',p.cmd('commit',choices=self.choices(plan)))
  p.close();self.probe=None;p=self.open();plan=p.preview(self.id)
  (self.backup/'public/same.txt').write_text('externally changed')
  self.assertIn('error',p.cmd('commit',choices=self.choices(plan)))
  self.assertEqual((self.pub/'same.txt').read_text(),'current file')
  self.assertEqual(sql(self.data,"select title from chat_sessions where id='conflict'"),[('Current conflict',)])
 def test_recovery_preserves_external_file_changes_instead_of_overwriting(self):
  p=self.open({'PENCHAT_TEST_RESTORE_FAULT':'before_commit'});plan=p.preview(self.id)
  p.p.stdin.write(json.dumps(dict(op='commit',choices=self.choices(plan)))+'\n');p.p.stdin.flush()
  self.assertEqual(p.p.wait(timeout=15),77);p.close();self.probe=None
  (self.pub/'same.txt').write_text('external change after interruption')
  result=subprocess.run([str(BINARY),str(self.data),str(self.pub),'--init'],capture_output=True)
  self.assertNotEqual(result.returncode,0)
  self.assertTrue((self.data/'.rescue/restore-journal.json').exists())
  self.assertEqual((self.pub/'same.txt').read_text(),'external change after interruption')
 def test_file_and_db_errors_roll_back_everything(self):
  for fault in ['db_error','file_error']:
   p=self.open({'PENCHAT_TEST_RESTORE_FAULT':fault});plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
   result=p.cmd('commit',choices=self.choices(plan));self.assertIn('error',result)
   self.assertEqual(sql(self.data,"select output_text from chat_messages where id='message-conflict'")[0][0],'current text')
   self.assertEqual((self.pub/'same.txt').read_text(),'current file');self.assertFalse((self.pub/'new.txt').exists())
   p.close();self.probe=None
 def test_interrupted_public_file_staging_is_removed_without_touching_original(self):
  p=self.open({'PENCHAT_TEST_RESTORE_FAULT':'before_file_rename'});plan=p.preview(self.id)
  p.p.stdin.write(json.dumps(dict(op='commit',choices=self.choices(plan)))+'\n');p.p.stdin.flush()
  self.assertEqual(p.p.wait(timeout=15),77);p.close();self.probe=None
  self.open();self.assertFalse(list(self.pub.rglob('.penchat-restore-*.part')))
  self.assertEqual((self.pub/'same.txt').read_text(),'current file')
  self.assertEqual(sql(self.data,"select title from chat_sessions where id='conflict'"),[('Current conflict',)])
 def test_process_interruption_recovers_from_durable_marker(self):
  p=self.open({'PENCHAT_TEST_RESTORE_FAULT':'before_commit'});plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  p.cmd('q',sql='PRAGMA cache_size=1')
  p.p.stdin.write(json.dumps(dict(op='commit',choices=self.choices(plan)))+'\n');p.p.stdin.flush();self.assertEqual(p.p.wait(timeout=15),77);p.close();self.probe=None
  self.open();self.assertEqual(sql(self.data,"select output_text from chat_messages where id='message-conflict'")[0][0],'current text');self.assertEqual((self.pub/'same.txt').read_text(),'current file');self.assertFalse((self.pub/'new.txt').exists())
  receipt=json.loads((self.data/'.rescue/restore-receipt.json').read_text());self.assertFalse(receipt['ok'])
 def test_committed_interruption_finishes_without_repeating_restore(self):
  p=self.open({'PENCHAT_TEST_RESTORE_FAULT':'after_commit'});plan=p.preview(self.id);self.assertNotIn('error',plan,plan)
  p.p.stdin.write(json.dumps(dict(op='commit',choices=self.choices(plan)))+'\n');p.p.stdin.flush();self.assertEqual(p.p.wait(timeout=15),77);p.close();self.probe=None
  self.open();self.assertEqual(sql(self.data,"select output_text from chat_messages where id='message-conflict'")[0][0],'backup text');self.assertEqual((self.pub/'same.txt').read_text(),'backup file')
  self.assertTrue(json.loads((self.data/'.rescue/restore-receipt.json').read_text())['ok']);self.assertFalse((self.data/'.rescue/restore-journal.json').exists())
if __name__=='__main__':unittest.main()
