#!/usr/bin/env python3
"""Capture host-owned launcher/IME screens using a disposable app database."""
import pathlib,sys,tempfile,time
import acceptance as d
from chat_acceptance import close_app,start,pull,push,isolate_fixture_assets,restore_fixture_assets
sys.path.insert(0,str(d.ROOT/'tests'))
from test_reasoning_ui import seed
if __name__=='__main__':
    state=d.state();assert state['view']=='chat' and not state['keyboard'] and not state['generating']
    assert d.adb('test -x /tmp/penchat-ui-tap && echo yes')=='yes'
    out=d.ROOT/'.work/ui-audit/after/device';out.mkdir(parents=True,exist_ok=True)
    host=d.adb('pidof miniapp')
    close_app();pull(d.DATA+'/penchat.db',out/'before.db')
    try:
        isolate_fixture_assets()
        with tempfile.TemporaryDirectory() as folder:
            seed(pathlib.Path(folder),1);push(pathlib.Path(folder)/'penchat.db',d.DATA+'/penchat.db')
        start()
        d.tap(500,232);d.official_editor()
        d.adb('miniapp_cli capture /tmp/penchat-ui-ime.png');time.sleep(.3)
        pull('/tmp/penchat-ui-ime.png',out/'system-ime.png')
        d.tap(35,35);d.cancel_native_editor()
        d.adb('kill -KILL '+d.pid());d.wait(lambda:not d.pid())
        d.wait(lambda:not d.adb('pidof penchat-scanner || true'))
        time.sleep(.5)
        d.adb('miniapp_cli capture /tmp/penchat-ui-launcher.png');time.sleep(.3)
        pull('/tmp/penchat-ui-launcher.png',out/'launcher-error.png')
        d.tap(122,218);d.wait(d.pid);d.view('chat')
        assert d.adb('pidof miniapp')==host
    finally:
        close_app();push(out/'before.db',d.DATA+'/penchat.db');restore_fixture_assets();start()
        pull(d.DATA+'/penchat.db',out/'restored.db')
        assert (out/'before.db').read_bytes()==(out/'restored.db').read_bytes()
        for name in ['before.db','restored.db']:(out/name).unlink()
    print('System IME and launcher error captured; retry works, DB restored, host unchanged.')
