#!/usr/bin/env python3
"""Actual LVGL + scanner child; no live sensor or real API credentials."""
import http.server, json, os, pathlib, signal, sqlite3, tempfile, threading, time
from test_lvgl_app import Native, ROOT, wait_for
import test_lvgl_app as transport
BUNDLE=pathlib.Path(os.environ.get('PENCHAT_SCAN_BUNDLE','/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018'))
def snapshot(app,name):
    output=ROOT/'.work/scanner-ui';output.mkdir(parents=True,exist_ok=True)
    app.process.send_signal(signal.SIGUSR1);time.sleep(.2)
    (output/(name+'.png')).write_bytes((app.directory/'lvgl-diagnostic.png').read_bytes())
def scan(app,n=33):
    wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
    app.send('scanner.test',command='test.press',pressed=True)
    wait_for(lambda:app.diag()['view']=='scan')
    for i in range(n):app.send('scanner.test',command='test.frame',sequence=i)
    app.send('scanner.test',command='test.press',pressed=False)
    wait_for(lambda:app.diag()['scan_result'].get('type')=='finished')
    return app.diag()
def run():
    env={'PENCHAT_SCANNER_BINARY':os.environ.get('PENCHAT_SCANNER_TEST_BINARY',str(ROOT/'build/scanner-host/penchat-scanner')),
      'PENCHAT_SCAN_RESOURCES':str(BUNDLE/'resources/illumination'),'PENCHAT_SCANNER_TEST':'1',
      'LD_LIBRARY_PATH':str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread')}
    previous={k:os.environ.get(k) for k in env};os.environ.update(env);transport.port=1
    try:
        with tempfile.TemporaryDirectory(prefix='penchat-scanner-ui-') as directory:
            app=Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',str(ROOT/'build/host/penchat-lvgl'))),pathlib.Path(directory))
            try:
                original=app.diag()['session']
                wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
                app.send('scanner.test',command='test.press',pressed=True)
                wait_for(lambda:app.diag()['view']=='scan')
                snapshot(app,'capturing')
                app.tap(40,32);wait_for(lambda:app.diag()['view']=='chat')
                finished=scan(app)
                assert finished['scan_result']['usable']
                assert finished['summary']['sessions']==0 and not app.read_messages()
                output=ROOT/'.work/scanner-ui';output.mkdir(parents=True,exist_ok=True)
                snapshot(app,'result')
                app.tap(840,210) # discard, without creating a chat row
                wait_for(lambda:app.diag()['view']=='chat')
                assert app.diag()['session']==original and app.diag()['summary']['sessions']==0
                scan(app)
                app.tap(840,102);app.tap(840,102)
                wait_for(lambda:app.diag()['view']=='chat')
                rows=app.read_messages();assert len(rows)==1 and rows[0][0]=='user',rows
                with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
                    asset=db.execute('select stored_path,display_src,source_type,metadata_json from message_assets').fetchone()
                assert pathlib.Path(asset[0]).exists() and pathlib.Path(asset[1]).exists()
                assert asset[2]=='scan' and json.loads(asset[3])['counts']['committed']==33
                assert pathlib.Path(asset[1]).stat().st_size<1024*1024
                # History can render the thumbnail after the pending original is deleted.
                wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
                assert not list((pathlib.Path(directory)/'.scan-pending').glob('*.png'))
                app.open_navigation();assert app.diag()['navigation_open']
                wait_for(lambda:app.diag()['scan_released'])
                app.tap(800,80)
                scan(app,0);assert not app.diag()['scan_result']['usable']
                snapshot(app,'empty')
                app.tap(840,102);assert app.diag()['view']=='scan' # empty cannot confirm
                app.tap(40,32);wait_for(lambda:app.diag()['view']=='chat')
                wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
                app.send('scanner.test',command='test.press',pressed=True)
                wait_for(lambda:app.diag()['view']=='scan')
                for _ in range(2):app.send('scanner.test',command='test.frame')
                app.send('scanner.test',command='test.frame',rawSequence=0)
                wait_for(lambda:app.diag()['scan_result'].get('type')=='finished')
                assert app.diag()['scan_result']['status']!=1
                assert app.diag()['scan_result']['usable']
                snapshot(app,'partial')
                app.send('scanner.test',command='test.press',pressed=False)
                app.tap(840,210);wait_for(lambda:app.diag()['view']=='chat')
                # Background cancels even a full pending result and never appends it.
                scan(app,1)
                app.send('visibility',visible=False)
                wait_for(lambda:app.diag()['scan_released'])
                app.send('visibility',visible=True)
                assert len(app.read_messages())==1 and app.diag()['view']=='chat'
                app.edit('扫描后仍可输入')
                assert app.diag()['view']=='chat'
            finally:app.close()
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),transport.Server)
        transport.port=server.server_port
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            for generating in (False,True):
                with tempfile.TemporaryDirectory(prefix='penchat-scan-send-') as directory:
                    binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',str(ROOT/'build/host/penchat-lvgl')))
                    app=Native(binary,pathlib.Path(directory),model='scroll' if generating else 'fixture')
                    app.close()
                    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
                        db.execute("insert or replace into app_settings values('penchat_settings',?,1)",(json.dumps({'autoSendCapturedContent':True}),))
                        db.execute("insert or replace into app_settings values('auto_title_enabled','false',1)")
                    app=Native(binary,pathlib.Path(directory))
                    try:
                        transport.Server.requests.clear()
                        if generating:
                            app.edit('已有回复继续生成');app.tap(908,232)
                            wait_for(lambda:len(transport.Server.requests)==1 and app.diag()['generating'])
                        scan(app)
                        app.tap(840,102)
                        wait_for(lambda:app.diag()['view']=='chat')
                        if generating:
                            # Confirmation attaches without replacing/cancelling the ongoing reply.
                            assert app.diag()['generating']
                            assert len(transport.Server.requests)==1
                            app.tap(908,232)
                            wait_for(lambda:not app.diag()['generating'])
                        else:
                            wait_for(lambda:len(transport.Server.requests)==1)
                            wire=json.dumps(transport.Server.requests[0][1])
                            assert 'data:image/png;base64,' in wire
                            wait_for(lambda:any(r[0]=='assistant' and r[1]=='complete' for r in app.read_messages()))
                    finally:app.close()
        finally:server.shutdown();server.server_close()
    finally:
        for k,v in previous.items():
            if v is None:os.environ.pop(k,None)
            else:os.environ[k]=v
    print('Scanner LVGL: review before persistence, discard, duplicate confirm, thumbnail storage, menu/IME/background release, empty-result exclusion, automatic image sending and active-generation preservation passed.')
if __name__=='__main__':run()
