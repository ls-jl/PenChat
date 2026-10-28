#!/usr/bin/env python3
"""Real verified TLS, persistent process, callback ownership and bounded curl pools."""
import collections,http.server,json,os,pathlib,queue,socket,ssl,struct,subprocess,tempfile,threading,time,urllib.parse
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_HTTP_POOL_PROBE',ROOT/'build/host/penchat-http-pool-probe'))
OUT=ROOT/'.work/http-pool-0.2.12';OUT.mkdir(parents=True,exist_ok=True)
class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    def get_request(self):
        raw,address=super().get_request()
        time.sleep(.08) # Observable cold TLS/accept cost, never model compute time.
        client=self.context.wrap_socket(raw,server_side=True)
        with self.guard:
            self.accepted+=1;client.serial=self.accepted
        return client,address
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def finish(self):
        try:super().finish()
        except (OSError,ssl.SSLError):pass
        with self.server.guard:self.server.closed+=1
    def reply(self,status,body,media='application/json',close=False):
        self.send_response(status);self.send_header('Content-Type',media);self.send_header('Content-Length',str(len(body)))
        if close:self.send_header('Connection','close');self.close_connection=True
        self.end_headers();self.wfile.write(body);self.wfile.flush()
    def do_GET(self):self.serve()
    def do_POST(self):self.serve()
    def serve(self):
        path=urllib.parse.urlsplit(self.path).path
        body=self.rfile.read(int(self.headers.get('Content-Length',0)))
        with self.server.guard:self.server.requests.append((path,self.connection.serial,dict(self.headers),body))
        try:
            if path=='/hold':time.sleep(float(urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).get('t',['2'])[0]))
            if path=='/rst':
                self.close_connection=True;self.connection.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0));self.connection.close();return
            if path in ('/drain','/hang','/overflow'):
                self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Transfer-Encoding','chunked');self.end_headers()
                def chunk(data):self.wfile.write(f'{len(data):X}\r\n'.encode()+data+b'\r\n');self.wfile.flush()
                chunk(b'END\n\n');time.sleep(.35 if path=='/drain' else .1 if path=='/overflow' else 2)
                chunk(b'x'*70000 if path=='/overflow' else b': tail\n\n');self.wfile.write(b'0\r\n\r\n');self.wfile.flush();return
            if path=='/chat/completions':
                frames=[{'choices':[{'index':0,'delta':{'content':'answer'},'finish_reason':None}]},
                        {'choices':[{'index':0,'delta':{},'finish_reason':'stop'}]},
                        {'choices':[],'usage':{'prompt_tokens':10,'completion_tokens':2,'total_tokens':12}}]
                wire=''.join('data: '+json.dumps(x)+'\n\n' for x in frames)+'data: [DONE]\n\n'
                self.reply(200,wire.encode(),'text/event-stream');return
            if path=='/responses':
                answer={'type':'response.completed','response':{'status':'completed','output':[{'type':'message','role':'assistant','content':[{'type':'output_text','text':'answer'}]}]}}
                self.reply(200,('data: '+json.dumps(answer)+'\n\n').encode(),'text/event-stream');return
            if path=='/messages':
                frames=[{'type':'message_start','message':{'role':'assistant','content':[],'usage':{'input_tokens':10,'output_tokens':0}}},
                  {'type':'content_block_start','index':0,'content_block':{'type':'text','text':''}},
                  {'type':'content_block_delta','index':0,'delta':{'type':'text_delta','text':'answer'}},
                  {'type':'content_block_stop','index':0}, {'type':'message_delta','delta':{'stop_reason':'end_turn'},'usage':{'output_tokens':2}}, {'type':'message_stop'}]
                self.reply(200,''.join('event: '+x['type']+'\ndata: '+json.dumps(x)+'\n\n' for x in frames).encode(),'text/event-stream');return
            if path=='/models':self.reply(200,b'{"data":[{"id":"fixture"}]}');return
            self.reply(401 if path=='/401' else 200,b'{"ok":true}',close=path=='/close')
        except (BrokenPipeError,ConnectionResetError,OSError,ssl.SSLError):self.close_connection=True
class Probe:
    def __init__(self,ca,argv=None,log=None):
        self.log_path=pathlib.Path(log or OUT/'pool-runtime.log');self.log=self.log_path.open('w')
        env=dict(os.environ)
        if ca:env['PENCHAT_TEST_CA']=str(ca)
        else:env.pop('PENCHAT_TEST_CA',None)
        self.p=subprocess.Popen(argv or [str(BINARY)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True,bufsize=1,env=env)
        self.received={};self.q=queue.Queue();self.counter=0
        def reader():
            for line in self.p.stdout:
                try:item=json.loads(line)
                except json.JSONDecodeError:continue
                # Old adbd shells echo stdin; acknowledgements/results have one
                # of these fields, while echoed command objects do not.
                if any(k in item for k in ('returned_ms','pool','ok')):self.q.put(item)
            self.q.put({'eof':True})
        self.thread=threading.Thread(target=reader,daemon=True);self.thread.start()
    def start(self,**kwargs):
        self.counter+=1;identity=str(self.counter);self.p.stdin.write(json.dumps({'id':identity,**kwargs})+'\n');self.p.stdin.flush();return identity
    def get(self,identity,timeout=10):
        end=time.monotonic()+timeout
        while identity not in self.received:
            item=self.q.get(timeout=max(.01,end-time.monotonic()));assert 'eof' not in item,(self.p.poll(),self.log_path.read_text())
            self.received[item['id']]=item
        return self.received.pop(identity)
    def call(self,**kwargs):return self.get(self.start(**kwargs))
    def close(self):
        if self.p.poll() is None:
            self.p.stdin.write('{"op":"exit"}\n');self.p.stdin.flush();self.p.stdin.close()
        assert self.p.wait(timeout=8)==0,self.log_path.read_text();self.log.close()
def wait(fn,timeout=6):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if fn():return
        time.sleep(.04)
    raise AssertionError('Timed out')
def run():
    with tempfile.TemporaryDirectory(prefix='penchat-tls-pool-') as folder:
        folder=pathlib.Path(folder);key=folder/'key.pem';ca=folder/'ca.pem'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(key),'-out',str(ca),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost,IP:127.0.0.1','-addext','basicConstraints=critical,CA:TRUE'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        server=Server(('127.0.0.1',0),Handler);server.context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);server.context.load_cert_chain(ca,key)
        server.guard=threading.Lock();server.accepted=server.closed=0;server.requests=[]
        threading.Thread(target=server.serve_forever,daemon=True).start();base=f'https://127.0.0.1:{server.server_port}'
        untrusted=Probe(None,log=OUT/'untrusted-runtime.log')
        try:
            rejected=untrusted.call(url=base+'/normal')
            assert rejected['status']==0 and 'certificate' in rejected['error'].lower(),rejected
        finally:untrusted.close()
        p=Probe(ca);checks={'untrusted_rejected':True}
        def request(path='/normal',**kw):return p.call(url=base+path,**kw)
        def count(path):return sum(x[0]==path for x in server.requests)
        try:
            assert p.call(op='diag')['pool']['idle_seconds']==90
            assert not server.requests, 'An untrusted peer must never receive an HTTP request'
            cold=request();accepted=server.accepted;warm=request();assert cold['status']==warm['status']==200 and not cold['error'] and not warm['error']
            assert cold['timings']['new_connections']==1 and warm['timings']['connection_reused']
            assert server.accepted==accepted and warm['timings']['tls_ms']==0
            checks['cold']=cold['timings'];checks['warm']=warm['timings']
            # Observe the real default deadline, without a transfer triggering
            # CURLOPT_MAXAGE_CONN. Polling diagnostics must not refresh the pool.
            time.sleep(89)
            assert p.call(op='diag')['pool']['sockets']==1
            wait(lambda:p.call(op='diag')['pool']['sockets']==0,timeout=4)
            assert request()['timings']['new_connections']==1
            checks['default_90_expiry']=True
            assert request(provider='other')['timings']['new_connections']==1
            assert request(revision='new')['timings']['new_connections']==1
            assert request(headers={'X-Secret':'do-not-log'})['timings']['new_connections']==1
            assert 'do-not-log' not in json.dumps(p.call(op='diag'))
            p.call(op='invalidate',provider='p');assert request()['timings']['new_connections']==1
            # Logical completion releases the caller while HTTP tail remains service-owned.
            request();tail=request('/drain',marker='END\n\n');assert not tail['error'] and tail['returned_ms']<300,tail
            wait(lambda:p.call(op='diag')['pool'].get('draining',0)==0)
            assert request()['timings']['connection_reused']
            for path in ['/hang','/overflow']:
                r=request(path,marker='END\n\n');assert not r['error'] and r['status']==200,r
                wait(lambda:p.call(op='diag')['pool'].get('draining',0)==0)
                assert request()['timings']['new_connections']==1
            before=count('/rst');r=request('/rst',method='POST',body='a nonempty request')
            assert r['error'] and count('/rst')==before+1,r
            r=request('/401');assert r['status']==401
            request('/close');assert request()['timings']['new_connections']==1
            # All three protocol parsers retain end markers, final usage and timings.
            for api in ['responses','chat','anthropic']:
                model={'name':'fixture','provider_id':'protocol','api_key':'fake-key','base_url':base,'provider_metadata':{'api':api}}
                r=p.call(op='stream',model=model);assert r['events'][-1]['type']=='done',r
                state=next(e['data'] for e in reversed(r['events']) if e['type']=='response_state')
                assert state['output']=='answer' and state['metadata']['first_body_ms'] is not None
                assert 'timings' in state['metadata']['http']
                if api=='chat':assert state['metadata']['usage']['total_tokens']==12
            checks['protocols']=['responses','chat','anthropic']
            # Concurrency, queue cancellation even when all active slots are occupied.
            active=[p.start(url=base+'/hold?t=2',provider=f'parallel{i}') for i in range(4)]
            wait(lambda:p.call(op='diag')['pool'].get('active')==4)
            queued=[p.start(url=base+'/normal',provider=f'queued{i}') for i in range(8)]
            wait(lambda:p.call(op='diag')['pool'].get('queued')==8)
            overflow=request();assert '队列已满' in overflow['error'],overflow
            p.call(op='cancel',target=queued[0]);r=p.get(queued.pop(0),timeout=1);assert r['error']=='cancelled',r
            for identity in active+queued:
                r=p.get(identity);assert r['status']==200 and not r['error'],r
            d=p.call(op='diag')['pool'];assert d['pools']<=8 and d['sockets']<=8,d
            checks['bounded']=d
            # Full idle caches must not turn a same-provider concurrent request
            # into a socket-allocation failure; reclaim another idle partition.
            for i in range(8):assert request(provider=f'capacity{i}')['status']==200
            hold=p.start(url=base+'/hold?t=2',provider='capacity7')
            wait(lambda:p.call(op='diag')['pool']['active']==1)
            parallel=request(provider='capacity7');assert parallel['status']==200 and not parallel['error'],parallel
            assert p.get(hold)['status']==200
            checks['same_provider_capacity']=True
            # Max retention does not kill active requests; shortening re-evaluates age.
            p.call(op='configure',seconds=600);request(provider='expiry')
            hold=p.start(url=base+'/hold?t=22',provider='long-active',timeout=30)
            wait(lambda:p.call(op='diag')['pool'].get('active')==1)
            time.sleep(15.3);p.call(op='configure',seconds=15)
            wait(lambda:p.call(op='diag')['pool'].get('pools')==1)
            assert p.call(op='diag')['pool']['active']==1
            assert request(provider='expiry')['timings']['new_connections']==1
            p.call(op='cancel',target=hold);assert p.get(hold)['error']=='cancelled'
            # Proactive minimum TTL: observe socket closure without making a request.
            wait(lambda:p.call(op='diag')['pool'].get('active')==0)
            time.sleep(15.5);wait(lambda:p.call(op='diag')['pool'].get('sockets')==0)
            checks['idle_expiry']=True
            for invalid in [0,14,601]:assert p.call(op='configure',seconds=invalid)['pool']['idle_seconds']==90
            assert p.call(op='configure',seconds=600)['pool']['idle_seconds']==600
            # Shutdown releases queued/active/draining/idle handles and all sockets.
            hold=p.start(url=base+'/hold?t=2');wait(lambda:p.call(op='diag')['pool'].get('active')==1)
            p.call(op='shutdown');assert p.get(hold)['error']=='cancelled'
            assert p.call(op='diag')['pool']['sockets']==0
            checks['shutdown_clean']=True
        finally:
            p.close();server.shutdown();server.server_close()
        (OUT/'tls-results.json').write_text(json.dumps(checks,indent=2))
    print('HTTP pool: verified TLS reuse/expiry, 3 protocols, detached drain, no POST replay, credential isolation, bounded concurrency/queue cancellation and shutdown passed.')

if __name__=='__main__':run()
