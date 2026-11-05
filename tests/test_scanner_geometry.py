#!/usr/bin/env python3
"""Incorrect tail pose through real scanner IPC and both LVGL layouts."""
import os, pathlib, signal, tempfile, time
import test_lvgl_app as t
from test_scanner_process import BUNDLE, ROOT

def run():
    env = {
        'PENCHAT_SCANNER_BINARY': os.environ.get('PENCHAT_SCANNER_TEST_BINARY', str(ROOT/'build/scanner-host/penchat-scanner')),
        'PENCHAT_SCAN_RESOURCES': str(BUNDLE/'resources/illumination'),
        'PENCHAT_SCANNER_TEST': '1', 'PENCHAT_TEST_SCAN_BAD_TAIL': '1',
        'LD_LIBRARY_PATH': str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread')}
    old = {k: os.environ.get(k) for k in env}; os.environ.update(env); t.port = 1
    out = ROOT/'.work/scanner-geometry-ui'; out.mkdir(parents=True, exist_ok=True)
    try:
        for width, height in [(936, 280), (568, 1210)]:
            with tempfile.TemporaryDirectory(prefix='penchat-geometry-') as folder:
                app = t.Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', ROOT/'build/host/penchat-lvgl')),
                               pathlib.Path(folder), screen={'width':width, 'height':height, 'direction':0})
                try:
                    t.wait_for(lambda:app.diag()['scanner'].get('type')=='ready')
                    app.send('scanner.test', command='test.press', pressed=True)
                    t.wait_for(lambda:app.diag()['view']=='scan')
                    held = width > height
                    count = 56 if held else 32
                    for i in range(count): app.send('scanner.test', command='test.frame', sequence=i)
                    if not held: app.send('scanner.test', command='test.press', pressed=False)
                    t.wait_for(lambda:app.diag()['scan_result'].get('type')=='finished')
                    result = app.diag()['scan_result']
                    assert result['status']!=1 and result['usable'] and result['reason']=='geometry_unstable', result
                    assert result['counts']['committed']==30 and result['counts']['unlocated']==count-30, result
                    if held: app.send('scanner.test', command='test.press', pressed=False)
                    assert not app.read_messages() and app.diag()['summary']['sessions']==0
                    t.wait_for(lambda:app.diag()['scanner'].get('type')=='preview')
                    time.sleep(.15) # The final preview is deliberately read off the capture thread.
                    image = pathlib.Path(folder)/'lvgl-diagnostic.png'
                    app.process.send_signal(signal.SIGUSR1)
                    t.wait_for(lambda:image.exists() and image.read_bytes().endswith(b'IEND\xaeB`\x82'))
                    (out/f'partial-{width}x{height}.png').write_bytes(image.read_bytes())
                    # Only explicit confirmation may attach the clear prefix.
                    app.tap(width-100 if width>height else width//2, 54 if width>height else height-90)
                    t.wait_for(lambda:app.diag()['view']=='chat')
                    assert len(app.read_messages())==1
                finally: app.close()
    finally:
        for k,v in old.items():
            if v is None: os.environ.pop(k,None)
            else: os.environ[k]=v
    print('Geometry tail: explicit partial, preserved prefix, no automatic attachment, landscape/portrait passed.')

if __name__=='__main__':run()
