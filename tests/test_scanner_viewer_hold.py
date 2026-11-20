#!/usr/bin/env python3
"""Scanner pen gating and local viewer retention; mock capture, no device IO."""
import json, os, pathlib, tempfile
from test_scanner_process import Scanner, BUNDLE
import test_lvgl_app as t
from test_web_images_ui import fixture


def worker_gate():
    for profile in ('orange','y02','y07','s6'):
        with tempfile.TemporaryDirectory(prefix='penchat-pen-gate-') as directory:
            scanner = Scanner(directory, profile=profile, resources=t.ROOT/'assets/scanner')
            try:
                for _ in range(4):
                    scanner.send('pen_input',pen_input_enabled=False)
                    state = scanner.receive('pen_input')
                    assert state['hardware_armed'] and not state['pen_input_enabled']
                    warmup = state['model_prepared_us']
                    scanner.send('test.press',pressed=True)
                    scanner.send('pen_input',pen_input_enabled=False)
                    state = scanner.receive('pen_input')
                    assert state['scanId']==0 and state['model_prepared_us']==warmup
                    scanner.send('pen_input',pen_input_enabled=True)
                    state = scanner.receive('pen_input')
                    assert state['scanId']==0 and state['hardware_armed']
                    scanner.send('test.press',pressed=True)  # Held across resume.
                    scanner.send('pen_input',pen_input_enabled=True)
                    assert scanner.receive('pen_input')['scanId']==0
                    scanner.send('test.press',pressed=False)
                started = scanner.press()
                assert started['pen_input_enabled'] and started['hardware_armed']
                scanner.send('pen_input',pen_input_enabled=False)
                state = scanner.receive('pen_input')
                assert state['scanId']==0 and state['hardware_armed']
                scanner.scan = 0
                scanner.send('disarm')
                assert not scanner.receive('released')['hardware_armed']
                scanner.generation += 1
                scanner.send('arm',pen_input_enabled=False)
                state = scanner.receive('ready')
                assert not state['pen_input_enabled'] and state['model_prepared_us']==warmup
            finally:
                scanner.close()


def viewer_gate():
    env = {'PENCHAT_SCANNER_BINARY':str(t.ROOT/'build/scanner-host/penchat-scanner'),
           'PENCHAT_SCAN_RESOURCES':str(BUNDLE/'resources/illumination'),
           'PENCHAT_SCANNER_TEST':'1',
           'LD_LIBRARY_PATH':str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread')}
    old = {k:os.environ.get(k) for k in env}
    os.environ.update(env)
    t.port = 1
    try:
        for width,height in [(640,260),(960,266),(568,1210)]:
            with tempfile.TemporaryDirectory(prefix='penchat-viewer-gate-') as directory:
                folder = pathlib.Path(directory)
                fixture(folder)
                app = t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False,screen={'width':width,'height':height,'direction':0})
                try:
                    t.wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
                    before = app.diag()['scanner']
                    t.wait_for(lambda:len(app.diag()['chat_images'])==2 and all(p['width']>1 for p in app.diag()['chat_images']))
                    for _ in range(8):
                        photo = min(app.diag()['chat_images'],key=lambda p:p['y'])
                        if 4<=photo['y'] and photo['y']+photo['height']<height-8:break
                        app.settings_swipe(65 if photo['y']<4 else -65)
                    for _ in range(4):
                        photo = min(app.diag()['chat_images'],key=lambda p:p['y'])
                        app.tap(photo['x']+photo['width']//2,photo['y']+photo['height']//2)
                        t.wait_for(lambda:app.diag()['image_viewer'].get('loaded',False))
                        t.wait_for(lambda:app.diag()['scanner'].get('pen_input_enabled') is False)
                        state = app.diag()['scanner']
                        assert state['hardware_armed'] and state['worker_pid']==before['worker_pid'] and state['generation']==before['generation']
                        app.send('scanner.test',command='test.press',pressed=True)
                        app.tap(width-38-(24 if height>width else 0),48 if height>width else 30)
                        t.wait_for(lambda:not app.diag()['image_viewer']['open'])
                        t.wait_for(lambda:app.diag()['scanner'].get('pen_input_enabled') is True)
                        state = app.diag()
                        assert state['view']=='chat' and state['scanner']['worker_pid']==before['worker_pid']
                        assert state['scanner']['generation']==before['generation'] and state['scanner']['model_prepared_us']==before['model_prepared_us']
                        app.send('scanner.test',command='test.press',pressed=False)
                    app.send('scanner.test',command='test.press',pressed=True)
                    t.wait_for(lambda:app.diag()['view']=='scan')
                    app.send('visibility',visible=False)
                    t.wait_for(lambda:app.diag()['scan_released'])
                finally:
                    app.close()
    finally:
        for k,v in old.items():
            if v is None:os.environ.pop(k,None)
            else:os.environ[k]=v

if __name__=='__main__':
    worker_gate()
    viewer_gate()
    print('Pen gating, held-key resume, viewer lease/model retention and background release passed')
