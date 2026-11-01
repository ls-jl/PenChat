#!/usr/bin/env python3
"""Actual native feedback queue, allowlist, persistence and fault-server tests."""
import http.server,json,os,pathlib,subprocess,tempfile,threading,time,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_FEEDBACK_PROBE',ROOT/'build/host/penchat-feedback-probe'))
class Server(http.server.BaseHTTPRequestHandler):
    requests=[];statuses=[];reply=None;retry_after=None;delay=0
    def log_message(self,*a):pass
    def do_POST(self):
        data=self.rfile.read(int(self.headers['Content-Length']))
        type(self).requests.append((json.loads(data),self.headers.get('Authorization'),data,self.headers.get('Host')))
        delay=type(self).delay
        if delay:time.sleep(delay)
        status=type(self).statuses.pop(0) if type(self).statuses else 201
        body=type(self).reply if type(self).reply is not None else b'{"ok":true}'
        self.send_response(status)
        self.send_header('Content-Length',str(len(body)))
        if self.retry_after:self.send_header('Retry-After',str(self.retry_after))
        self.end_headers()
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass
class Probe:
    def __init__(self,directory,endpoint,nodes=(),unit=1):
        self.directory=pathlib.Path(directory);self.directory.mkdir(parents=True,exist_ok=True)
        config=self.directory/'test-config.json';config.write_text(json.dumps(dict(endpoint=endpoint,token='FAKE_FEEDBACK_TOKEN',nodes=list(nodes),retry_unit_ms=unit)))
        env=dict(os.environ,PENCHAT_TEST_FEEDBACK_CONFIG=str(config))
        self.log=open(self.directory/'probe.log','w')
        self.p=subprocess.Popen([str(BINARY),str(directory)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True,env=env)
        self.wait(lambda s:s.get('ready') or s.get('error'))
    def cmd(self,op,**args):
        self.p.stdin.write(json.dumps(dict(op=op,args=args))+'\n');self.p.stdin.flush()
        line=self.p.stdout.readline()
        assert line, 'Probe exited: '+str(self.p.poll())
        return json.loads(line)
    def wait(self,fn,timeout=6):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            state=self.cmd('state')
            if fn(state):return state
            time.sleep(.02)
        raise AssertionError(state)
    def preview(self,**runtime):
        before=self.cmd('state').get('preview_id');self.cmd('prepare',**runtime)
        state=self.wait(lambda s:s.get('preview_ready') and s.get('preview_id')!=before)
        text=''
        for i in range(state['preview_pages']):
            self.p.stdin.write(json.dumps({'op':'preview','page':i})+'\n');self.p.stdin.flush();text+=json.loads(self.p.stdout.readline())['text']
        return state,json.loads(text)
    def submit(self):
        s,_=self.preview();self.cmd('submit',revision=s['revision'])
    def close(self,crash=False):
        if self.p.poll() is None:
            if crash:self.p.stdin.write('{"op":"crash"}\n');self.p.stdin.flush()
            else:self.p.stdin.write('{"op":"quit"}\n');self.p.stdin.flush()
            self.p.wait(timeout=8)
        self.p.stdin.close();self.p.stdout.close();self.log.close()
class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
        cls.endpoint='http://127.0.0.1:%d/api/logs'%cls.server.server_port
    @classmethod
    def tearDownClass(cls):cls.server.shutdown();cls.server.server_close()
    def setUp(self):
        Server.requests=[];Server.statuses=[];Server.reply=None;Server.retry_after=None;Server.delay=0
        self.tmp=tempfile.TemporaryDirectory(prefix='penchat-feedback-');self.probes=[]
    def tearDown(self):
        for p in self.probes:p.close()
        self.tmp.cleanup()
    def probe(self,**kw):
        p=Probe(self.tmp.name,self.endpoint,**kw);self.probes.append(p);return p
    def test_whitelist_screen_preview_and_success(self):
        p=self.probe();s,report=p.preview(secret='SECRET_RUNTIME',generating=True)
        encoded=json.dumps(report)
        self.assertNotIn('SECRET_',encoded)
        self.assertNotIn('FAKE_FEEDBACK_TOKEN',encoded)
        self.assertEqual(report['screen']['configured']['width'],936)
        self.assertEqual(report['screen']['configured']['direction'],270)
        self.assertEqual(report['runtime'],{'generating':True})
        p.cmd('submit',revision=s['revision']);state=p.wait(lambda s:len(s.get('receipts',[]))==1)
        self.assertEqual(state['pending'],0);self.assertEqual(len(Server.requests),1)
        self.assertEqual(Server.requests[0][1],'Bearer FAKE_FEEDBACK_TOKEN')
        self.assertEqual(list((p.directory/'.feedback/reports').glob('*.json')),[])
    def test_three_retries_stable_body_and_id(self):
        Server.statuses=[500,507,503,201]
        p=self.probe(nodes=['127.0.0.1','127.0.0.1']);p.submit();p.wait(lambda s:len(s.get('receipts',[]))==1)
        self.assertEqual(len(Server.requests),4)
        self.assertEqual(len({x[2] for x in Server.requests}),1)
    def test_nonretryable_and_invalid_confirmations(self):
        for index,(status,body) in enumerate([(401,b'bad key'),(404,b'html'),(413,b'too big'),(201,b'{"ok":false}'),(200,b'{"ok":true}'),(201,b'<html>')]):
            Server.requests=[];Server.statuses=[status];Server.reply=body
            folder=pathlib.Path(self.tmp.name)/str(index);p=Probe(folder,self.endpoint);self.probes.append(p)
            p.submit();s=p.wait(lambda s:s.get('records') and s['records'][0]['state']=='manual')
            self.assertEqual(s['records'][0]['attempts'],1);self.assertEqual(len(Server.requests),1)
            self.assertNotIn(body.decode(),json.dumps(s))
    def test_exhaustion_stop_delete_manual_retry(self):
        Server.statuses=[500]*4;p=self.probe();p.submit()
        s=p.wait(lambda s:s.get('records') and s['records'][0]['state']=='manual');r=s['records'][0]
        self.assertEqual(r['attempts'],4);self.assertEqual(len(Server.requests),4)
        p.cmd('retry',id=r['id']);p.wait(lambda s:len(s.get('receipts',[]))==1)
        Server.statuses=[401];p.submit();s=p.wait(lambda s:s.get('records') and s['records'][0]['state']=='manual')
        p.cmd('delete',id=s['records'][0]['id']);p.wait(lambda s:not s['records'])
    def test_retry_after_restart_and_stop(self):
        Server.statuses=[429,201];Server.retry_after=2
        p=self.probe();p.submit();s=p.wait(lambda s:s.get('records') and s['records'][0]['state']=='queued' and s['records'][0]['attempts']==1)
        ident=s['records'][0]['id'];self.assertGreater(s['records'][0]['due_ms'],time.time()*1000+1000)
        p.close();p=self.probe();s=p.cmd('state');self.assertEqual(s['records'][0]['attempts'],1)
        p.cmd('stop_auto',id=ident);p.wait(lambda s:not s['records'][0]['auto']);time.sleep(2.1)
        self.assertEqual(len(Server.requests),1)
        p.cmd('retry',id=ident);p.wait(lambda s:len(s.get('receipts',[]))==1)
    def test_capacity_unconfigured_and_generation(self):
        p=self.probe();p.cmd('draft',adapt=True,description='用户选择填写的内容')
        p.wait(lambda s:s.get('adapt'));p.cmd('keys.start',step='scan');s=p.wait(lambda s:s.get('collecting'))
        generation=s['key_generation'];p.cmd('keys.result',generation=generation-1,step='scan',status='complete',events=[])
        time.sleep(.1);self.assertEqual(p.cmd('state')['steps'],{})
        p.cmd('keys.skip',step='scan');p.wait(lambda s:s.get('steps',{}).get('scan',{}).get('status')=='skipped')
        p.cmd('keys.start',step='home');p.wait(lambda s:s.get('collecting'));p.cmd('background');time.sleep(.1);self.assertTrue(p.cmd('state')['collecting'])
        p.cmd('draft',adapt=False);p.wait(lambda s:not s.get('adapt'));self.assertEqual(p.cmd('state')['steps'],{})
        Server.statuses=[401]*10
        for i in range(10):
            p.cmd('draft',description=str(i));p.wait(lambda s:s.get('description')==str(i));p.submit()
            p.wait(lambda s:len(s['records'])==i+1 and s['records'][-1]['state']=='manual')
        p.submit();s=p.wait(lambda s:'已满' in s.get('error',''));self.assertEqual(s['pending'],10)
    def test_corrupt_record_preserved_and_deletable(self):
        p=self.probe();p.close();folder=p.directory/'.feedback/reports'
        ident='12345678-1234-4234-8234-123456789abc'
        (folder/(ident+'.json')).write_text('{corrupted')
        temporary=folder/(ident+'.json.'+ident+'.part');temporary.write_text('incomplete')
        p=self.probe();s=p.wait(lambda s:s.get('records'))
        self.assertFalse(temporary.exists())
        self.assertEqual(s['records'][0]['state'],'invalid');self.assertEqual(len(Server.requests),0)
        p.cmd('retry',id=ident);p.wait(lambda s:'损坏' in s.get('error',''))
        p.cmd('delete',id=ident);p.wait(lambda s:not s['records']);self.assertFalse((folder/(ident+'.json')).exists())
    def test_partial_home_restart_and_description_limit(self):
        p=self.probe();p.cmd('draft',adapt=True,description='kept');p.wait(lambda s:s.get('adapt'))
        p.cmd('keys.start',step='home');s=p.wait(lambda s:s.get('collecting'))
        p.cmd('keys.result',generation=s['key_generation'],step='home',status='pressed',events=[{'code':777,'value':1}])
        p.wait(lambda s:s.get('steps',{}).get('home',{}).get('status')=='pressed');p.close(crash=True)
        p=self.probe();s=p.cmd('state');self.assertEqual(s['steps']['home']['status'],'release_missing')
        self.assertFalse(s['collecting']);p.cmd('draft',description='字'*2001)
        s=p.wait(lambda s:'2000' in s.get('error',''));self.assertEqual(s['description'],'kept')
    def test_cancel_active_and_large_receipt(self):
        Server.delay=2;p=self.probe();p.submit()
        p.wait(lambda s:s.get('records') and s['records'][0]['state']=='sending')
        start=time.monotonic();p.close();self.assertLess(time.monotonic()-start,1.5)
        time.sleep(2.1);Server.delay=0;Server.reply=b'x'*70000
        p=self.probe();p.wait(lambda s:s.get('records') and s['records'][0]['state']=='manual')
        self.assertGreaterEqual(len(Server.requests),1)
    def test_verified_tls_route_preserves_host_and_sni(self):
        import ssl
        cert=pathlib.Path(self.tmp.name)/'cert.pem';key=pathlib.Path(self.tmp.name)/'key.pem'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1',
            '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
            '-keyout',str(key),'-out',str(cert)],check=True,capture_output=True)
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,key)
        names=[];context.set_servername_callback(lambda sock,name,ctx:names.append(name))
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
        server.socket=context.wrap_socket(server.socket,server_side=True)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        previous=os.environ.get('PENCHAT_TEST_CA');os.environ['PENCHAT_TEST_CA']=str(cert)
        Server.statuses=[500,201]
        try:
            p=Probe(pathlib.Path(self.tmp.name)/'tls',f'https://localhost:{server.server_port}/api/logs',nodes=['127.0.0.1'])
            self.probes.append(p);p.submit();p.wait(lambda s:len(s.get('receipts',[]))==1)
            self.assertEqual(len(Server.requests),2)
            self.assertEqual([x[3] for x in Server.requests],[f'localhost:{server.server_port}']*2)
            self.assertEqual(names,['localhost','localhost'])
        finally:
            if previous is None:os.environ.pop('PENCHAT_TEST_CA',None)
            else:os.environ['PENCHAT_TEST_CA']=previous
            server.shutdown();server.server_close()
    def test_runtime_write_failure_does_not_stick_or_duplicate(self):
        p=self.probe();p.cmd('draft',adapt=True);p.wait(lambda s:s.get('adapt'))
        folder=p.directory/'.feedback';folder.chmod(0o500)
        try:
            p.cmd('keys.start',step='scan');s=p.wait(lambda s:s.get('action')=='error')
            self.assertFalse(s['collecting'])
        finally:folder.chmod(0o700)
        p.cmd('draft',adapt=False);p.wait(lambda s:not s.get('adapt'))
        preview,_=p.preview();Server.statuses=[401];folder.chmod(0o500)
        try:
            p.cmd('submit',revision=preview['revision'])
            s=p.wait(lambda s:s.get('records') and s['records'][0]['state']=='manual')
            self.assertFalse(s['preview_ready']);self.assertEqual(s['pending'],1)
            p.cmd('submit',revision=preview['revision']);p.wait(lambda s:'变化' in s.get('error',''))
            self.assertEqual(len(Server.requests),1)
        finally:folder.chmod(0o700)
    def test_storage_failure_isolated(self):
        (pathlib.Path(self.tmp.name)/'.feedback').write_text('not a directory')
        p=self.probe();s=p.cmd('state');self.assertFalse(s['ready']);self.assertTrue(s['error'])
        p.cmd('prepare');p.wait(lambda s:s.get('action')=='error');self.assertEqual(Server.requests,[])
    def test_private_generator_unconfigured(self):
        target=pathlib.Path(self.tmp.name)/'public.hpp'
        subprocess.run(['python3',str(ROOT/'scripts/feedback-config.py'),'',str(target)],check=True,capture_output=True)
        self.assertIn('FeedbackBuild_token[] = "";',target.read_text())
        source=pathlib.Path(self.tmp.name)/'bad.json';source.write_text('{"endpoint":"http://bad","token":"DO_NOT_ECHO_THIS"}')
        result=subprocess.run(['python3',str(ROOT/'scripts/feedback-config.py'),str(source),str(target)],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertNotIn('DO_NOT_ECHO_THIS',result.stdout+result.stderr)
    def test_log_bounded_and_previous(self):
        p=self.probe();p.close();p=self.probe();s,report=p.preview()
        self.assertTrue(report['diagnostics']['previous_run'])
        p.cmd('logstress');s,report=p.preview()
        self.assertEqual(len(report['diagnostics']['current_run']),500)
        self.assertNotIn('NEVER_LOG',json.dumps(report))
        self.assertLess((p.directory/'.feedback/diagnostic-current.json').stat().st_size,256*1024)
        self.assertLessEqual(len(report['diagnostics']['current_run']),500)
        self.assertLess(len(json.dumps(report).encode()),1024*1024)
if __name__=='__main__':unittest.main()
