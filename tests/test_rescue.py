#!/usr/bin/env python3
"""Actual isolated rescue worker, local HTTP, real archive/hash and backups."""
import hashlib,http.server,json,os,pathlib,shutil,sqlite3,subprocess,tempfile,threading,time,unittest,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
APP=json.loads((ROOT/'package.json').read_text())
BINARY=pathlib.Path(os.environ.get('PENCHAT_RESCUE_TEST_BINARY',ROOT/'build/host/rescue/penchat-rescue-probe'))
class Server(http.server.BaseHTTPRequestHandler):
 routes={};posts=[];delay=0;post_status=201
 def log_message(self,*args):pass
 def do_HEAD(self):
  data=self.routes.get(self.path)
  if data is None:self.send_error(404);return
  self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
 def do_GET(self):
  data=self.routes.get(self.path)
  if data is None:self.send_error(404);return
  self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
  try:
   for i in range(0,len(data),1024):self.wfile.write(data[i:i+1024]);self.wfile.flush();time.sleep(self.delay)
  except (BrokenPipeError,ConnectionResetError):pass
 def do_POST(self):
  body=self.rfile.read(int(self.headers['Content-Length']));self.posts.append((json.loads(body),dict(self.headers)))
  reply=b'{"ok":true}';self.send_response(self.post_status);self.send_header('Content-Length',str(len(reply)));self.end_headers();self.wfile.write(reply)
class Probe:
 def __init__(self,root,url,retry_unit_ms=1,extra_env=None,abi_bits=64):
  self.root=pathlib.Path(root);self.data=self.root/'data';self.public=self.root/'public';self.workspace=self.root/'workspace'
  for d in [self.data,self.public,self.workspace]:d.mkdir(exist_ok=True)
  self.p=subprocess.Popen([str(BINARY)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,**(extra_env or {})))
  self.p.stdin.write(json.dumps(dict(root=url,abi_bits=abi_bits,retry_unit_ms=retry_unit_ms,endpoint=url+'logs',data=str(self.data),public=str(self.public),workspace=str(self.workspace)))+'\n');self.p.stdin.flush()
  assert json.loads(self.p.stdout.readline())['platform']=='rk3562'
 def cmd(self,op,**data):
  self.p.stdin.write(json.dumps(dict(op=op,**data))+'\n');self.p.stdin.flush();return json.loads(self.p.stdout.readline())
 def wait(self,predicate,seconds=12):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   state=self.cmd('state')
   if predicate(state):return state
   time.sleep(.03)
  raise AssertionError('Rescue state timed out: '+json.dumps(state,ensure_ascii=False))
 def close(self):
  if self.p.poll() is None:self.cmd_quit()
  self.p.wait(timeout=8);error=self.p.stderr.read();assert self.p.returncode==0,error
  self.p.stdin.close();self.p.stdout.close();self.p.stderr.close()
 def cmd_quit(self):self.p.stdin.write('{"op":"quit"}\n');self.p.stdin.flush()
class Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start();cls.url='http://127.0.0.1:'+str(cls.server.server_port)+'/'
 @classmethod
 def tearDownClass(cls):cls.server.shutdown();cls.server.server_close()
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='penchat-rescue-test-');self.root=pathlib.Path(self.temp.name);Server.routes={};Server.posts=[];Server.delay=0;Server.post_status=201
  self.probe=Probe(self.root,self.url)
  with sqlite3.connect(self.probe.data/'penchat.db') as db:db.execute('create table schema_meta(key text,value text)');db.execute("insert into schema_meta values('schema_version','3')")
 def tearDown(self):self.probe.close();self.temp.cleanup()
 def release(self,version='0.3.2',machine=183,compatible=True,extra=None):
  elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01';elf[18:20]=machine.to_bytes(2,'little')
  path=self.root/'package.amr'
  with zipfile.ZipFile(path,'w') as z:
   z.writestr('manifest.json',json.dumps(dict(appid=APP['appid'],version=version)));z.writestr('app.js.bin','fixture');z.writestr('assets/bin/penchat-lvgl',elf)
   if extra:z.writestr(*extra)
  data=path.read_bytes();sha=hashlib.sha256(data).hexdigest();base='/penchat-rk3562/'+version+'/'
  metadata=dict(format=1,platform='rk3562',version=version,file='package.amr',sha256=sha,size=len(data),channel='candidate',schema=dict(min=3 if compatible else 2,max=3 if compatible else 2))
  Server.routes['/penchat-rk3562/']=b'<html>Index of <a href="'+version.encode()+b'/">version</a></html>'
  Server.routes[base+'release.json']=json.dumps(metadata).encode();Server.routes[base+'package.amr']=data;Server.routes[base+'package.amr.sha256']=(sha+'  package.amr\n').encode()
  return data,sha,base
 def listed(self):
  self.probe.cmd('list');return self.probe.wait(lambda s:not s['busy'] and s['status'] in ['available','error'])
 def prepared(self,sha,**args):
  self.probe.cmd('prepare',id=sha,**args);return self.probe.wait(lambda s:not s['busy'] and s['status'] in ['ready_to_install','error','cancelled'])
 def test_unsupported_host_abi_blocks_catalog_prepare_and_install(self):
  self.probe.close();self.probe=Probe(self.root,self.url,abi_bits=32)
  self.probe.cmd('list');state=self.probe.wait(lambda s:s['status']=='error')
  self.assertIn('宿主架构',state['message'])
  self.probe.cmd('prepare',id='stale',force=True);state=self.probe.wait(lambda s:s['status']=='error')
  self.assertIn('宿主架构',state['message'])
  self.assertIn('宿主架构',self.probe.cmd('authorize',ticket={})['error'])
 def test_user_release_notes_and_selected_install_warning(self):
  _,_,base=self.release();r=json.loads(Server.routes[base+'release.json']);r.update(release_notes='修复公式显示。\n改善菜单操作。',install_warning='旧版没有降级入口。',known_issues='legacy technical source');Server.routes[base+'release.json']=json.dumps(r).encode()
  row=self.listed()['releases'][0];self.assertEqual(row['release_notes'],r['release_notes']);self.assertEqual(row['install_warning'],r['install_warning'])
  r['release_notes']='x'*8193;Server.routes[base+'release.json']=json.dumps(r).encode();self.assertEqual(self.listed()['releases'],[])
 def test_list_hash_backup_and_same_version_verification(self):
  data,sha,_=self.release(version='0.3.3');self.assertEqual(len(self.listed()['releases']),1)
  (self.probe.data/'assets').mkdir();(self.probe.data/'assets/picture').write_bytes(b'private-pixels');(self.probe.public/'instructions').write_text('preserve')
  state=self.prepared(sha);self.assertEqual(state['status'],'ready_to_install',state)
  ticket=state['ticket'];backup=pathlib.Path(ticket['backup']['path']);self.assertEqual((backup/'data/assets/picture').read_bytes(),b'private-pixels');self.assertEqual((backup/'public/instructions').read_text(),'preserve')
  self.assertTrue((backup/'manifest.json').exists())
  result=self.probe.cmd('authorize',ticket=ticket);self.assertEqual(result['state'],'requested')
  self.assertIn('error',self.probe.cmd('authorize',ticket=ticket))
  self.probe.cmd('cancel');self.assertEqual(self.probe.cmd('state')['status'],'installing')
  binary=self.probe.workspace/'assets/bin/penchat-lvgl';binary.parent.mkdir(parents=True);binary.write_bytes(b'old')
  info=dict(appid=APP['appid'],version='0.3.3',installPath=str(self.probe.workspace))
  self.assertIn('error',self.probe.cmd('verify',info=info))
  with zipfile.ZipFile(self.root/'package.amr') as z:binary.write_bytes(z.read('assets/bin/penchat-lvgl'))
  self.assertTrue(self.probe.cmd('verify',info=info)['verified'])
  self.probe.cmd('callback',operation=ticket['operation'],state='unknown');self.assertEqual(self.probe.cmd('state')['ticket']['state'],'installed')
 def test_force_only_bypasses_schema(self):
  _,sha,_=self.release(compatible=False);self.assertFalse(self.listed()['releases'][0]['compatible'])
  self.assertEqual(self.prepared(sha)['status'],'error');self.assertEqual(self.prepared(sha,force=True)['status'],'ready_to_install')
 def test_direct_rollback_requires_explicit_second_confirmation(self):
  _,sha,_=self.release();self.listed()
  for options in [dict(backup=False),dict(backup=False,skip_backup_confirmed=False),dict(backup='false',skip_backup_confirmed=True)]:
   self.assertEqual(self.prepared(sha,**options)['status'],'error')
  self.assertNotIn('ticket',self.probe.cmd('state'))
 def test_direct_rollback_skips_only_backup_and_records_the_choice(self):
  _,sha,_=self.release();self.listed();s=self.prepared(sha,backup=False,skip_backup_confirmed=True)
  self.assertEqual(s['status'],'ready_to_install',s);self.assertIsNone(s['ticket']['backup']);self.assertTrue(s['ticket']['backup_skipped'])
  self.assertFalse((self.probe.data/'.rescue/backups').exists())
  self.assertNotIn('error',self.probe.cmd('authorize',ticket=s['ticket']))
 def test_direct_rollback_keeps_schema_and_package_validation(self):
  _,sha,_=self.release(compatible=False);self.listed()
  options=dict(backup=False,skip_backup_confirmed=True)
  self.assertEqual(self.prepared(sha,**options)['status'],'error')
  self.assertEqual(self.prepared(sha,force=True,**options)['status'],'ready_to_install')
 def test_direct_wrong_arch_and_unsafe_package_are_still_rejected(self):
  for options in [dict(machine=40),dict(extra=('../outside',b'bad'))]:
   _,sha,_=self.release(**options);self.listed();state=self.prepared(sha,backup=False,skip_backup_confirmed=True,force=True)
   self.assertEqual(state['status'],'error');self.assertNotIn('ticket',state)
 def test_backup_failure_does_not_implicitly_skip_but_direct_choice_can(self):
  _,sha,_=self.release();self.listed();external=self.root/'external';external.mkdir()
  (self.probe.data/'.rescue/backups').symlink_to(external,target_is_directory=True)
  self.assertEqual(self.prepared(sha)['status'],'error')
  s=self.prepared(sha,backup=False,skip_backup_confirmed=True);self.assertEqual(s['status'],'ready_to_install',s)
  self.assertEqual(list(external.iterdir()),[])
 def test_wrong_arch_cannot_be_forced(self):
  _,sha,_=self.release(machine=40,compatible=False);self.listed();state=self.prepared(sha,force=True)
  self.assertEqual(state['status'],'error');self.assertNotIn('ticket',state)
 def test_checksum_publication_incomplete_is_omitted(self):
  _,_,base=self.release();Server.routes[base+'package.amr.sha256']=b'0'*64
  self.assertEqual(self.listed()['releases'],[])
 def test_symlink_backup_fails_without_install(self):
  _,sha,_=self.release();self.listed();(self.probe.data/'outside').symlink_to('/etc/passwd')
  s=self.prepared(sha);self.assertEqual(s['status'],'error');self.assertNotIn('ticket',s)
 def test_unsafe_archive_is_rejected(self):
  _,sha,_=self.release(extra=('../outside',b'bad'));self.listed();self.assertEqual(self.prepared(sha)['status'],'error')
 def test_cancel_prepared_ticket_cannot_authorize(self):
  _,sha,_=self.release();self.listed();s=self.prepared(sha);self.probe.cmd('cancel');self.assertIn('error',self.probe.cmd('authorize',ticket=s['ticket']))
 def test_reports_are_minimal_and_deduplicated(self):
  (self.probe.data/'last-exit.json').write_text(json.dumps(dict(status=-1,pid=123,wait_errno=10,wait_status=-1,wait_status_available=False,ipc_eof=True,requested_signal=0,native_fault_signal=11,native_reason='runtime_error',native_phase='exception',reason='SECRET_CHAT',api_key='SECRET_PROVIDER_KEY')))
  self.probe.cmd('report',code='startup',secret='SECRET_PROVIDER_KEY',description='SECRET_CHAT');self.probe.wait(lambda s:s['report']['status']=='sent')
  rescue=Server.posts[-1][0]['rescue'];self.assertEqual(rescue['wait_errno'],10);self.assertFalse(rescue['wait_status_available']);self.assertEqual(rescue['native_fault_signal'],11);self.assertEqual(rescue['native_phase'],'exception');self.assertNotIn('reason',rescue)
  self.assertEqual(len(Server.posts),1);body,headers=Server.posts[0];self.assertNotIn('SECRET_',json.dumps(body));self.assertEqual(headers['Authorization'],'Bearer FAKE_RESCUE_TOKEN')
  self.probe.cmd('report',code='startup');time.sleep(.2);self.assertEqual(len(Server.posts),1)
  self.probe.close();self.probe=Probe(self.root,self.url);self.probe.cmd('report',code='startup');time.sleep(.2);self.assertEqual(len(Server.posts),1)
 def test_wrong_support_library_cannot_be_forced(self):
  wrong=bytearray(64);wrong[:6]=b'\x7fELF\x01\x01';wrong[18]=40;wrong[37]=4
  _,sha,_=self.release(extra=('assets/scanner/lib/injected.so',wrong));self.listed();self.assertEqual(self.prepared(sha,force=True)['status'],'error')
 def test_empty_directories_wal_ime_and_public_backup(self):
  _,sha,_=self.release();self.listed()
  for path in ['ime/empty','assets/empty']:(self.probe.data/path).mkdir(parents=True)
  (self.probe.data/'penchat.db-wal').write_bytes(b'wal');(self.probe.data/'penchat.db-shm').write_bytes(b'shm')
  (self.probe.public/'empty').mkdir();s=self.prepared(sha);self.assertEqual(s['status'],'ready_to_install',s)
  backup=pathlib.Path(s['ticket']['backup']['path']);manifest=json.loads((backup/'manifest.json').read_text());self.assertEqual(manifest['source_version'],APP['version']);self.assertEqual(manifest['source_schema'],3);self.assertEqual(manifest['appid'],APP['appid']);self.assertTrue((backup/'data/ime/empty').is_dir());self.assertTrue((backup/'public/empty').is_dir())
  self.assertEqual((backup/'data/penchat.db-wal').read_bytes(),b'wal');self.assertEqual((backup/'data/penchat.db-shm').read_bytes(),b'shm')
  self.assertFalse((backup/'data/.rescue').exists())
 def test_corrupt_database_requires_explicit_force(self):
  _,sha,_=self.release();(self.probe.data/'penchat.db').write_bytes(b'corrupt')
  self.assertFalse(self.listed()['releases'][0]['compatible']);self.assertEqual(self.prepared(sha)['status'],'error')
  self.assertEqual(self.prepared(sha,force=True)['status'],'ready_to_install')
 def test_cancel_slow_download_and_no_ticket(self):
  _,sha,base=self.release(extra=('assets/padding',os.urandom(60000)));Server.delay=.03;self.listed()
  self.probe.cmd('prepare',id=sha);self.probe.wait(lambda s:s['status']=='downloading');self.probe.cmd('cancel')
  s=self.probe.wait(lambda s:s['status']=='cancelled');self.assertNotIn('ticket',s)
 def test_pending_ticket_blocks_resubmission_after_restart(self):
  _,sha,_=self.release();self.listed();s=self.prepared(sha);self.probe.cmd('authorize',ticket=s['ticket'])
  self.probe.close();self.probe=Probe(self.root,self.url);self.assertIn('error',self.probe.cmd('prepare',id=sha))
 def test_per_fingerprint_dedup_retries_and_private_summary(self):
  folder=self.probe.data/'.feedback';folder.mkdir()
  (folder/'diagnostic-current.json').write_text(json.dumps([dict(category='lifecycle',event='runtime_error',time_ms=1,fields=dict(signal=11,secret='SECRET')),dict(category='private',event='SECRET',fields=dict())]))
  Server.post_status=503;self.probe.cmd('report',code='module');self.probe.wait(lambda s:s['report']['status']=='failed')
  self.assertEqual(len(Server.posts),4);ids={b['event_id'] for b,h in Server.posts};self.assertEqual(len(ids),1)
  self.assertEqual(Server.posts[0][0]['diagnostics'][0]['fields'],dict(signal=11));self.assertNotIn('SECRET',json.dumps(Server.posts))
  Server.post_status=201;self.probe.cmd('report',code='module',retry=True);self.probe.wait(lambda s:s['report']['status']=='sent')
  self.assertEqual(Server.posts[-1][0]['event_id'],next(iter(ids)))
  self.probe.cmd('report',code='timeout');self.probe.wait(lambda s:s['report']['status']=='sent' and s['report']['id'] not in ids)
  count=len(Server.posts);self.probe.cmd('report',code='module');time.sleep(.15);self.assertEqual(len(Server.posts),count)
 def test_pending_report_survives_restart_with_same_identity(self):
  self.probe.close();self.probe=Probe(self.root,self.url,retry_unit_ms=1000);Server.post_status=503
  self.probe.cmd('report',code='timeout');self.probe.wait(lambda s:s['report']['status']=='sending')
  end=time.monotonic()+2
  while not Server.posts and time.monotonic()<end:time.sleep(.005)
  self.assertEqual(len(Server.posts),1);report_id=Server.posts[0][0]['event_id'];self.probe.close()
  record=next((self.probe.data/'.rescue/reports').glob('*.json'));value=json.loads(record.read_text());self.assertEqual(value['state'],'pending')
  value['due_ms']=0;record.write_text(json.dumps(value));Server.post_status=201;self.probe=Probe(self.root,self.url)
  self.probe.wait(lambda s:s['report']['status']=='sent');self.assertEqual(Server.posts[-1][0]['event_id'],report_id)
 def test_disk_full_blocks_backup_and_ticket(self):
  if 'ASAN_OPTIONS' in os.environ:self.skipTest('LD_PRELOAD space fault conflicts with ASan runtime; native pressure test runs unsanitized')
  source=self.root/'full.c';library=self.root/'full.so'
  source.write_text('#include <sys/statvfs.h>\n#include <string.h>\nint statvfs(const char *p,struct statvfs *s){(void)p;memset(s,0,sizeof(*s));s->f_frsize=4096;return 0;}\n')
  subprocess.run(['cc','-shared','-fPIC',str(source),'-o',str(library)],check=True)
  self.probe.close();self.probe=Probe(self.root,self.url,extra_env=dict(LD_PRELOAD=str(library)))
  _,sha,_=self.release();self.listed()
  for options in [dict(),dict(backup=False,skip_backup_confirmed=True)]:
   state=self.prepared(sha,**options);self.assertEqual(state['status'],'error');self.assertNotIn('ticket',state)
 def test_supporting_rescue_pair_is_only_cross_abi_exception(self):
  wrong=bytearray(64);wrong[:6]=b'\x7fELF\x01\x01';wrong[18]=40;wrong[37]=4
  _,sha,_=self.release(extra=('libs/arm/libjsapi_penchat_rescue_'+'a'*16+'.so',wrong));self.listed();self.assertEqual(self.prepared(sha)['status'],'ready_to_install')
 def test_native_running_blocks_backup_even_with_trailing_slash(self):
  _,sha,_=self.release();self.listed();hold=self.root/'penchat-lvgl';hold.symlink_to(BINARY)
  p=subprocess.Popen([str(hold),'--hold','--data-dir',str(self.probe.data)+'/'])
  try:
   state=self.prepared(sha);self.assertEqual(state['status'],'error');self.assertNotIn('ticket',state)
  finally:p.terminate();p.wait()
 def test_rescue_root_symlink_never_reads_or_writes_outside(self):
  self.probe.close();shutil.rmtree(self.probe.data/'.rescue')
  external=self.root/'external';external.mkdir();(external/'sentinel').write_text('untouched')
  (self.probe.data/'.rescue').symlink_to(external,target_is_directory=True)
  self.probe=Probe(self.root,self.url);_,sha,_=self.release();self.assertEqual(len(self.listed()['releases']),1)
  self.probe.cmd('report',code='startup');self.probe.wait(lambda s:s['report']['status']=='failed')
  self.assertEqual(self.prepared(sha)['status'],'error');self.assertEqual(list(external.iterdir()),[external/'sentinel']);self.assertEqual(Server.posts,[])
 def test_corrupt_receipt_and_ticket_do_not_break_bootstrap(self):
  self.probe.close();rescue=self.probe.data/'.rescue';(rescue/'reports').mkdir(exist_ok=True)
  (rescue/'reports'/('a'*64+'.json')).write_text(json.dumps(dict(state=7,payload=dict(rescue=[]))))
  (rescue/'install.json').write_text(json.dumps(dict(operation=3,state=[])))
  self.probe=Probe(self.root,self.url);self.assertNotIn('ticket',self.probe.cmd('state'))
  self.release();self.assertEqual(len(self.listed()['releases']),1)
if __name__=='__main__':unittest.main()
