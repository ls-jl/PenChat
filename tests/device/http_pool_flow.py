"""Explicit-device settings, main-app reuse and source-identical native TLS probe."""
import json,subprocess,sys,threading,time

class DeviceServer:
    def __init__(self,root,serial,out,cert=None,key=None):
        import queue
        self.serial=serial;self.requests=[];self.ready=queue.Queue();self.closed=0
        self.remote='/tmp/penchat-http-server-'+serial+('-tls' if cert else '-plain')
        subprocess.run(['adb','-s',serial,'push',str(root/'build/orange/penchat-tls-fixture'),self.remote],check=True,capture_output=True)
        subprocess.run(['adb','-s',serial,'shell','chmod 755 '+self.remote],check=True,capture_output=True)
        args=[]
        if cert:
            for source,suffix in [(cert,'.pem'),(key,'.key')]:
                subprocess.run(['adb','-s',serial,'push',str(source),self.remote+suffix],check=True,capture_output=True)
            subprocess.run(['adb','-s',serial,'shell','chmod 600 '+self.remote+'.pem '+self.remote+'.key'],check=True,capture_output=True)
            args=[self.remote+'.pem',self.remote+'.key']
        self.log=(out/('loopback-tls.log' if cert else 'loopback-http.log')).open('w')
        self.process=subprocess.Popen(['adb','-s',serial,'shell','exec '+self.remote+' '+' '.join(args)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True,bufsize=1)
        def read():
            for line in self.process.stdout:
                try:event=json.loads(line)
                except json.JSONDecodeError:continue
                if event['event']=='ready':self.pid=event['pid'];self.ready.put(event['port'])
                elif event['event']=='request':self.requests.append(event)
                elif event['event']=='closed':self.closed+=1
        threading.Thread(target=read,daemon=True).start();self.server_port=self.ready.get(timeout=10)
    def shutdown(self):
        if self.process.poll() is None:
            self.process.stdin.write('stop\n');self.process.stdin.flush();self.process.stdin.close()
        assert self.process.wait(timeout=8)==0
        self.log.close()
        subprocess.run(['adb','-s',self.serial,'shell','rm -f '+self.remote+' '+self.remote+'.pem '+self.remote+'.key'],check=True,capture_output=True)
    def server_close(self):pass

def prepare(root,folder,data,serial):
    import shutil,sqlite3
    sys.path.insert(0,str(root/'tests'));from test_session_info import fixture
    server=DeviceServer(root,serial,folder.parent)
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();api='chat' if serial.endswith('113') else 'responses';fixture(folder,server.server_port,api)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('update providers set base_url=?,metadata_json=?,headers_json=?',
          (f'http://127.0.0.1:{server.server_port}/v1',json.dumps({'api':api}),json.dumps({'X-PenChat-Fixture':'local'})))
        db.execute('update message_assets set stored_path=?,original_path=?',(data+'/assets/original.png',)*2)
        for name in ['auto_title_enabled','auto_context_compression']:
            db.execute('insert or replace into app_settings values(?,?,1)',(name,'false'))
    return server,server,[(folder/'penchat.db',data+'/penchat.db'),(folder/'assets/original.png',data+'/assets/original.png')]

def run(c,handler):
    read,tap,wait=c['state'],c['tap'],c['wait'];W,H=c['W'],c['H']
    capture=c['capture'];serial=c['SERIAL'];out=c['OUT'];root=c['ROOT']
    assert read()['network_transport']['idle_seconds']==90
    def button(b):tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def key(k):
        item=next(v for v in read()['editor']['keys'] if v['key']==k);tap(item['x'],item['y'])
    def question():
        tap(W//2,H-34);wait(lambda:read()['editor']['active'])
        if read()['editor']['layout']!='en26':key('language')
        key('q');button(read()['editor']['actions']['done']);wait(lambda:not read()['editor']['active'])
        tap(W-52,H-34)
    timings=[]
    for n in range(2):
        question();wait(lambda:len(handler.requests)==n+1)
        wait(lambda:not read()['generating'] and not read()['storage']['unsaved'])
        timings.append(read()['network_transport']['last']);capture('http-pool-'+str(n))
    assert timings[0]['new_connections']==1 and timings[1]['connection_reused'],timings
    p=c['pid']();c['shell']('miniapp_cli start 8080222437664451');wait(lambda:not read()['visible'])
    assert read()['network_transport']['sockets']>0
    c['shell']('miniapp_cli start '+c['APP']['appid']);wait(lambda:read()['visible']);assert c['pid']()==p
    question();wait(lambda:len(handler.requests)==3);wait(lambda:not read()['generating'] and not read()['storage']['unsaved'])
    assert read()['network_transport']['last']['connection_reused']
    # Official editor cancellation does not flush the independent HTTP pools.
    tap(W//2,H-34);wait(lambda:read()['editor']['active']);button(read()['editor']['actions']['official']);wait(lambda:read()['keyboard'])
    assert read()['network_transport']['sockets']>0
    tap(35,35);wait(lambda:not read()['keyboard']);button(read()['editor']['actions']['cancel']);wait(lambda:not read()['editor']['active'])
    tap(40,32);wait(lambda:read()['navigation_open']);tap(W*2//3-70,H-30);wait(lambda:read()['view']=='settings')
    from settings_ui import activate
    activate(read,tap,c['settings_swipe'],'http_idle');wait(lambda:read()['view']=='http_settings');capture('http-settings-default')
    def seconds(value):
        button(read()['settings']['rows']['http_idle_seconds']['bounds']);wait(lambda:read()['editor']['active'])
        length=read()['editor']['length']
        for _ in range(length):key('delete')
        key('mode')
        for ch in str(value):key(ch)
        button(read()['editor']['actions']['done']);wait(lambda:not read()['editor']['active'])
        assert read()['network_transport']['idle_seconds']==value
    seconds(15);capture('http-settings-minimum');seconds(600);capture('http-settings-maximum')
    c['close']();c['start_app']();assert read()['network_transport']['idle_seconds']==600
    assert read()['network_transport'].get('sockets',0)==0
    # A test-only executable accepts a private CA; shipped native code has no CA override.
    sys.path.insert(0,str(root/'tests'));import test_http_pool as h
    ca=out/'http-test-ca.pem';private=out/'http-test-key.pem'
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(private),'-out',str(ca),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost,IP:127.0.0.1','-addext','basicConstraints=critical,CA:TRUE'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    server=DeviceServer(root,serial,out,ca,private);base=f'https://127.0.0.1:{server.server_port}'
    remote='/tmp/penchat-http-pool-'+serial;probe=None
    try:
        for source,dest in [(root/'build/orange/penchat-http-pool-probe',remote),(ca,remote+'.pem')]:
            subprocess.run(['adb','-s',serial,'push',str(source),dest],check=True,capture_output=True)
        c['shell']('chmod 755 '+remote)
        probe=h.Probe(None,['adb','-s',serial,'shell','PENCHAT_TEST_CA='+remote+'.pem '+remote],out/'http-probe.log')
        cold=probe.call(url=base+'/normal');warm=probe.call(url=base+'/normal')
        assert cold['status']==warm['status']==200 and not cold['error'] and not warm['error'],(cold,warm)
        assert cold['timings']['new_connections']==1 and warm['timings']['connection_reused']
        for api in ['responses','chat','anthropic']:
            model={'name':'fixture','provider_id':'native-tls','api_key':'fake-key','base_url':base,'provider_metadata':{'api':api}}
            response=probe.call(op='stream',model=model);assert response['events'][-1]['type']=='done',response
        pool=probe.call(op='diag')['pool'];assert pool['curl_version']=='8.17.0' and 'OpenSSL/1.1.1' in pool['tls_backend'],pool
        probe.call(op='configure',seconds=15);time.sleep(15.5)
        h.wait(lambda:probe.call(op='diag')['pool']['sockets']==0)
        probe.call(url=base+'/normal');probe.call(op='shutdown');assert probe.call(op='diag')['pool']['sockets']==0
        c['result']['http_pool']={'main_http_reused':True,'background_retention':True,'official_retention':True,'settings_15_600_restart':True,
           'tls_cold':cold['timings'],'tls_warm':warm['timings'],'native_curl':pool['curl_version'],'tls_backend':pool['tls_backend'],'three_apis':True,'proactive_expiry':True,'shutdown_clean':True}
    finally:
        if probe:probe.close()
        server.shutdown();server.server_close();c['shell']('rm -f '+remote+' '+remote+'.pem')
        private.unlink(missing_ok=True);ca.unlink(missing_ok=True)
