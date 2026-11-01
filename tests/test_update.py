#!/usr/bin/env python3
"""Actual native updater against controlled independent HTTP/TLS nodes."""
import hashlib,http.server,io,json,os,pathlib,socket,ssl,subprocess,tempfile,threading,time,unittest,urllib.parse,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_UPDATE_PROBE',ROOT/'build/host/penchat-update-probe'))
APPID=json.loads((ROOT/'package.json').read_text())['appid']
def package(version='0.2.21',appid=APPID,size=128000,extra=None):
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_STORED) as z:
        z.writestr('manifest.json',json.dumps(dict(appid=appid,version=version,name='PenChat')))
        z.writestr('app.js.bin',os.urandom(size))
        if extra:
            for k,v in extra.items():z.writestr(k,v)
    return out.getvalue()
class Node:
    def __init__(self,ip='127.0.0.1',port=0):
        self.files={};self.delay=0;self.rate=0;self.range=True;self.requests=[];self.fail=0;self.lock=threading.Lock();self.next=0
        node=self
        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            def log_message(self,*a):pass
            def do_HEAD(self):self.respond(True)
            def do_GET(self):self.respond(False)
            def respond(self,head):
                path=urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
                with node.lock:node.requests.append((time.monotonic(),self.command,path,self.headers.get('Range'),self.headers.get('Host')))
                if head:time.sleep(node.delay)
                if path.endswith('/'):
                    names=set()
                    for key in node.files:
                        if key.startswith(path):
                            tail=key[len(path):];names.add(tail.split('/')[0]+('/' if '/' in tail else ''))
                    body=('<html><h1>Index of '+path+'</h1>'+''.join('<a href="'+urllib.parse.quote(n)+'">'+n+'</a>' for n in sorted(names))+'</html>').encode();status=200
                else:body=node.files.get(path);status=200 if body is not None else 404
                if body is None:body=b'not found'
                length=len(body);etag='"'+hashlib.sha256(body).hexdigest()+'"';range_text=None
                if not head and self.headers.get('Range') and node.range and status==200:
                    if node.fail:
                        node.fail-=1;status=503;body=b'temporary'
                    else:
                        start,end=map(int,self.headers['Range'].replace('bytes=','').split('-'));body=body[start:end+1];status=206;range_text=f'bytes {start}-{end}/{length}'
                self.send_response(status);self.send_header('Content-Length',str(len(body)));self.send_header('ETag',etag)
                if range_text:self.send_header('Content-Range',range_text)
                self.end_headers()
                if head:return
                try:
                    for at in range(0,len(body),8192):
                        chunk=body[at:at+8192]
                        if node.rate and not path.endswith('.md') and path.endswith('.amr'):
                            with node.lock:
                                now=time.monotonic();target=max(now,node.next);node.next=target+len(chunk)/node.rate
                            time.sleep(max(0,target-time.monotonic()))
                        self.wfile.write(chunk);self.wfile.flush()
                except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
        self.server=http.server.ThreadingHTTPServer((ip,port),Handler);self.server.daemon_threads=True
        self.port=self.server.server_port;threading.Thread(target=self.server.serve_forever,daemon=True).start()
    def release(self,version='0.2.21',body=None,sha=True,note=b'## Improvements\n\nOffline verified update.'):
        body=body if body is not None else package(version)
        base='/penchat/'+version+'/';self.files[base+'PenChat.amr']=body
        if sha:self.files[base+'PenChat.amr.sha256']=(hashlib.sha256(body).hexdigest()+'  PenChat.amr\n').encode()
        if note is not None:self.files[base+'release-note.md']=note
        return body
    def close(self):self.server.shutdown();self.server.server_close()
class Probe:
    def __init__(self,directory,root,nodes=None,current='0.2.20',auto=False):
        self.path=pathlib.Path(directory);self.path.mkdir(parents=True,exist_ok=True)
        config=self.path/'config.json';config.write_text(json.dumps(dict(root=root,nodes=nodes or [],current=current,auto=auto)))
        env=dict(os.environ,NO_PROXY='*',no_proxy='*');self.log=open(self.path/'probe.log','a')
        self.p=subprocess.Popen([str(BINARY),str(config),str(self.path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True,env=env)
    def cmd(self,op,**args):
        data={'op':op,'args':args}
        if op=='cancel':data.update(args)
        self.p.stdin.write(json.dumps(data)+'\n');self.p.stdin.flush();line=self.p.stdout.readline()
        assert line, f'updater exited {self.p.poll()}: '+(self.path/'probe.log').read_text()
        if op not in ('state','cancel'):time.sleep(.05)
        return json.loads(line)
    def wait(self,fn,timeout=10):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            state=self.cmd('state')
            if fn(state):return state
            time.sleep(.02)
        raise AssertionError(state)
    def check(self,manual=True):
        before=self.cmd('state').get('generation',0);self.cmd('check',manual=manual);return self.wait(lambda s:s.get('generation',0)>before and s['status'] not in ('idle','checking'))
    def close(self):
        if self.p.poll() is None:
            self.p.stdin.write('{"op":"quit"}\n');self.p.stdin.flush();self.p.wait(timeout=4)
        self.p.stdin.close();self.p.stdout.close();self.log.close()
class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='penchat-update-');self.a=Node();self.b=Node('127.0.0.2',self.a.port);self.probes=[]
        self.root=f'http://localhost:{self.a.port}/penchat/'
    def tearDown(self):
        for p in self.probes:p.close()
        self.a.close();self.b.close();self.tmp.cleanup()
    def probe(self,nodes=None,current='0.2.20',auto=False,folder='data'):
        p=Probe(pathlib.Path(self.tmp.name)/folder,self.root,nodes or ['127.0.0.1','127.0.0.2'],current,auto);self.probes.append(p);return p
    def test_numeric_versions_fallback_notes_and_ignore(self):
        for v in ['0.2.9','0.2.21','0.2.22','0.2.100']:
            self.a.release(v)
        # Invalid newest version falls back, even though lower node is absent.
        self.a.files['/penchat/0.2.100/extra.amr']=b'x'
        self.a.files['/penchat/0.2.22/PenChat.amr.sha256']=b'bad hash'
        self.a.files['/penchat/9.0beta/x.amr']=b'x'
        self.a.files['/penchat/0.02.50/x.amr']=b'x'
        p=self.probe();s=p.check();self.assertEqual(s['release']['version'],'0.2.21')
        self.assertEqual([n['version'] for n in s['notes']],['0.2.21'])
        p.wait(lambda s:len(s['faults'])==2)
        p.cmd('decline');p.wait(lambda s:s['status']=='deferred')
        self.assertTrue(p.check(False)['suppressed']);self.assertFalse(p.check(True)['suppressed'])
        self.a.release('0.2.23');s=p.check(False);self.assertEqual(s['release']['version'],'0.2.23');self.assertFalse(s['suppressed'])
        self.assertEqual([n['version'] for n in s['notes']],['0.2.21','0.2.22','0.2.23'])
    def test_single_good_node_head_latency_and_same_file(self):
        body=self.a.release();self.b.release(body=body);self.a.delay=.05;self.b.delay=.005
        p=self.probe();s=p.check();self.assertEqual(s['release']['nodes'][0]['address'],'127.0.0.2');self.assertEqual(len(s['release']['nodes']),2)
        for node in (self.a,self.b):self.assertEqual(sum(x[1]=='HEAD' for x in node.requests),3)
        self.b.release(body=package(size=128000));s=p.check();self.assertEqual(len(s['release']['nodes']),1)
        del self.b.files['/penchat/0.2.21/PenChat.amr'];s=p.check();self.assertEqual(s['release']['nodes'][0]['address'],'127.0.0.1')
    def test_weak_confirmation_crc_identity_and_path_safety(self):
        cases=[('good',package(),False,True),('identity',package(appid='8000000000000000'),True,False),('unsafe',package(extra={'../evil':b'x'}),True,False),('hash',package(),True,False)]
        for name,body,sha,success in cases:
            self.a.files={};self.a.release(body=body,sha=sha)
            if name=='hash':self.a.files['/penchat/0.2.21/PenChat.amr.sha256']=b'0'*64
            p=self.probe(nodes=['127.0.0.1'],folder=name);s=p.check()
            if not sha:
                p.cmd('download');p.wait(lambda s:s['status']=='error');self.assertFalse((p.path/'.updates/download.part').exists())
            p.cmd('download',allow_weak=True,snapshot={'draft':'preserved'})
            s=p.wait(lambda s:s['status'] in ('ready','error'))
            self.assertEqual(s['status']=='ready',success,s)
            if success:self.assertEqual((p.path/'.updates/package.amr').read_bytes(),body)
            else:self.assertFalse((p.path/'.updates/download.part').exists())
    def test_range_fallback_cancel_and_ticket_recovery(self):
        body=self.a.release(body=package(size=3*1024*1024));self.a.range=False
        p=self.probe(nodes=['127.0.0.1']);p.check();p.cmd('download',snapshot={'draft':'new text','session':'temporary'})
        s=p.wait(lambda s:s['status']=='ready');self.assertFalse(s['statistics']['range']);self.assertEqual(s['statistics']['peak'],1)
        self.assertEqual(sum(x[3] is not None for x in self.a.requests),1)
        p.cmd('install_requested');s=p.wait(lambda s:s['status']=='installing');p.close()
        p=self.probe(nodes=['127.0.0.1']);s=p.cmd('state');self.assertEqual(s['status'],'uncertain');self.assertEqual(s['ticket']['state'],'requested')
        self.assertEqual(json.loads((p.path/'.updates/restore.json').read_text())['snapshot']['draft'],'new text')
        p.cmd('install_result',operation=s['ticket']['operation'],success=False);p.wait(lambda s:s['status']=='install_error');p.close()
        p=self.probe(nodes=['127.0.0.1']);self.assertEqual(p.cmd('state')['status'],'install_error');p.close()
        p=self.probe(nodes=['127.0.0.1'],current='0.2.21');self.assertEqual(p.cmd('state')['status'],'installed');self.assertFalse((p.path/'.updates/package.amr').exists())
    def test_cancel_then_restart_keeps_decline_and_removes_partial(self):
        self.a.release(body=package(size=3*1024*1024));self.a.rate=100000;p=self.probe(nodes=['127.0.0.1']);p.check();p.cmd('download')
        p.wait(lambda s:s.get('progress',{}).get('downloaded',0)>0);p.cmd('cancel',decline=True)
        p.wait(lambda s:s['status']=='deferred');self.assertFalse((p.path/'.updates/download.part').exists());p.close()
        p=self.probe(nodes=['127.0.0.1']);self.assertTrue(p.check(False)['suppressed'])
    def test_slow_primary_merges_ramps_and_retries(self):
        body=self.a.release(body=package(size=10*1024*1024));self.b.release(body=body);self.b.delay=.03;self.a.rate=180000;self.b.rate=950000
        p=self.probe();s=p.check();self.assertEqual(s['release']['nodes'][0]['address'],'127.0.0.1');p.cmd('download')
        s=p.wait(lambda s:s['status'] in ('ready','error'),timeout=75);self.assertEqual(s['status'],'ready',s)
        self.assertTrue(s['statistics']['dual']);self.assertLessEqual(s['statistics']['peak'],8);self.assertGreaterEqual(s['statistics']['peak'],5)
        self.assertEqual((p.path/'.updates/package.amr').read_bytes(),body)
        self.assertTrue(any(r[3] for r in self.b.requests));self.assertTrue(all(r[4]==f'localhost:{self.a.port}' for r in self.b.requests))
    def test_transient_ranges_retry_but_never_exceed_limit(self):
        self.a.release(body=package(size=2*1024*1024));self.a.fail=2;p=self.probe(nodes=['127.0.0.1']);p.check();p.cmd('download')
        s=p.wait(lambda s:s['status'] in ('ready','error'),timeout=12);self.assertEqual(s['status'],'ready',s)
        self.assertEqual(sum(r[3]=='bytes=0-1048575' for r in self.a.requests),3)
    def test_zip_crc_symlink_duplicate_identity_and_entrypoint(self):
        valid=package();outputs=[]
        broken=bytearray(valid);where=valid.find(b'app.js.bin')+len(b'app.js.bin')+60;broken[where]^=1;outputs.append(bytes(broken))
        outputs.append(package(extra={'/absolute':b'x'}));outputs.append(package(extra={'a/../../escape':b'x'}))
        for kind in ['symlink','duplicate','entrypoint']:
            buf=io.BytesIO()
            with zipfile.ZipFile(buf,'w') as z:
                z.writestr('manifest.json',json.dumps(dict(appid=APPID,version='0.2.21')))
                if kind!='entrypoint':z.writestr('app.js.bin',b'x')
                if kind=='symlink':
                    info=zipfile.ZipInfo('link');info.create_system=3;info.external_attr=0o120777<<16;z.writestr(info,'/etc/passwd')
                if kind=='duplicate':
                    import warnings
                    with warnings.catch_warnings():warnings.simplefilter('ignore');z.writestr('app.js.bin',b'changed')
            outputs.append(buf.getvalue())
        for i,body in enumerate(outputs):
            path=pathlib.Path(self.tmp.name)/f'bad-{i}.amr';path.write_bytes(body)
            r=subprocess.run([str(BINARY),'--validate',str(path),APPID,'0.2.21',hashlib.sha256(body).hexdigest()],capture_output=True,text=True)
            self.assertEqual(r.returncode,1,(i,r.stdout,r.stderr));self.assertIn('error',json.loads(r.stdout))

    def test_node_failure_continues_from_confirmed_peer(self):
        body=self.a.release(body=package(size=3*1024*1024));self.b.release(body=body);self.b.delay=.02
        p=self.probe();p.check();self.a.fail=50;p.cmd('download')
        s=p.wait(lambda s:s['status'] in ('ready','error'),timeout=15);self.assertEqual(s['status'],'ready',s)
        self.assertTrue(s['statistics']['dual']);self.assertEqual((p.path/'.updates/package.amr').read_bytes(),body)

    def test_above_two_mbps_keeps_single_node(self):
        body=self.a.release(body=package(size=3*1024*1024));self.b.release(body=body);self.b.delay=.03;self.a.rate=265000
        p=self.probe();p.check();p.cmd('download')
        s=p.wait(lambda s:s['status'] in ('ready','error'),timeout=25);self.assertEqual(s['status'],'ready',s);self.assertFalse(s['statistics']['dual'])
        self.assertFalse(any(r[3] for r in self.b.requests))

    def test_tls_sni_and_verification(self):
        cert=pathlib.Path(self.tmp.name)/'cert';key=pathlib.Path(self.tmp.name)/'key'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-keyout',str(key),'-out',str(cert)],capture_output=True,check=True)
        ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(cert,key);names=[];ctx.set_servername_callback(lambda sock,name,context:names.append(name))
        self.a.server.socket=ctx.wrap_socket(self.a.server.socket,server_side=True);self.root=self.root.replace('http:','https:');self.a.release()
        os.environ['PENCHAT_TEST_CA']=str(cert)
        try:
            p=self.probe(nodes=['127.0.0.1']);self.assertEqual(p.check()['status'],'available');p.cmd('download');self.assertEqual(p.wait(lambda s:s['status'] in ('ready','error'))['status'],'ready');self.assertTrue(names and all(n=='localhost' for n in names))
        finally:os.environ.pop('PENCHAT_TEST_CA',None)
if __name__=='__main__':unittest.main()
