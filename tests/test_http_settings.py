#!/usr/bin/env python3
"""Actual editor/settings behavior; no network, credentials or user data required."""
import os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/http-pool-0.2.12';OUT.mkdir(parents=True,exist_ok=True)
def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-http-settings-') as folder:
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        def value():return a.diag()['network_transport']['idle_seconds']
        def shot(name):
            a.process.send_signal(__import__('signal').SIGUSR1);p=pathlib.Path(folder)/'lvgl-diagnostic.png';t.wait_for(p.exists);(OUT/f'{w}-{name}.png').write_bytes(p.read_bytes());p.unlink()
        def submit(text,confirmed=True):
            row=a.diag()['settings']['rows']['http_idle_seconds']['bounds'];a.tap((row[0]+row[2])//2,(row[1]+row[3])//2)
            t.wait_for(lambda:a.diag()['editor']['active']);a.editor_action('official');r=a.receive('keyboard.open')
            assert r['multiline'] is False and r['inputType']=='EnUSOnly'
            a.send('keyboard.result',requestId=r['requestId'],confirmed=confirmed,text=text)
            t.wait_for(lambda:not a.diag()['keyboard'])
        try:
            assert value()==90;a.open_settings();a.setting('http_idle');assert a.diag()['view']=='http_settings';shot('default')
            for n in [15,600,90]:
                submit(str(n));t.wait_for(lambda:value()==n);assert not a.diag()['editor']['active'];shot(str(n))
            for bad in ['14','601','1.5','-15','abc','']:
                submit(bad);assert value()==90 and a.diag()['modal'];a.tap(w-70,h-44)
            submit('15');t.wait_for(lambda:value()==15);submit('600',False)
            assert a.diag()['editor']['active'] and value()==15
            a.editor_action('cancel');a.page_action('恢复默认');assert value()==90
            submit('600');t.wait_for(lambda:value()==600);a.tap(40,32);assert a.diag()['view']=='settings'
            assert a.diag()['scroll_y']>0;shot('settings-row')
            a.send('visibility',visible=False);a.send('visibility',visible=True);assert value()==600
        finally:a.close()
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:assert value()==600
        finally:a.close()
        for invalid in ['"90"','14','601','null']:
            with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:db.execute('update app_settings set value_json=? where key=?',(invalid,'http_idle_seconds'))
            a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
            try:assert value()==90
            finally:a.close()
    print(f'HTTP settings {w}x{h}: 90 default, 15/600 limits, invalid/cancel/reset, reader retention, background and restart passed.',flush=True)
if __name__=='__main__':
    for size in [(960,266),(936,280)]:run(*size)
