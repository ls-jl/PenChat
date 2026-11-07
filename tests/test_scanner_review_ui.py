#!/usr/bin/env python3
"""Scan review with real LVGL + mock scanner worker in three logical viewports."""
import os,pathlib,signal,tempfile
import test_lvgl_app as t
from test_scanner_ui import BUNDLE
from image_gestures import pinch,drag,capture
OUT=t.ROOT/'.work/scanner-review';OUT.mkdir(parents=True,exist_ok=True)
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))

def preview_tap(a):
    b=a.diag()['scan_preview_area'];assert len(b)==4,b
    a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)

def press(a):
    t.wait_for(lambda:a.diag()['scanner'].get('type')=='ready')
    a.send('scanner.test',command='test.press',pressed=True)
    t.wait_for(lambda:a.diag()['view']=='scan')

def finish(a,missing=0):
    for i in range(33):a.send('scanner.test',command='test.frame',rawSequence=i,rawTimestamp=100000+i*8400)
    if missing:a.send('scanner.test',command='test.frame',rawSequence=33+missing,rawTimestamp=500000)
    a.send('scanner.test',command='test.press',pressed=False)
    t.wait_for(lambda:a.diag()['scan_result'].get('type')=='finished')

def run():
    env={'PENCHAT_SCANNER_BINARY':str(t.ROOT/'build/scanner-host/penchat-scanner'),
         'PENCHAT_SCAN_RESOURCES':str(BUNDLE/'resources/illumination'),'PENCHAT_SCANNER_TEST':'1',
         'LD_LIBRARY_PATH':str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu')+':'+str(BUNDLE/'third_party/opencv-host/lib/x86_64-linux-gnu/openblas-pthread')}
    before={k:os.environ.get(k) for k in env};os.environ.update(env);t.port=1
    try:
        for w,h in [(960,266),(936,280),(568,1210)]:
            with tempfile.TemporaryDirectory(prefix='penchat-scan-review-ui-') as directory:
                root=pathlib.Path(directory);a=t.Native(BINARY,root,screen={'width':w,'height':h,'direction':0},official_adapter=False)
                try:
                    session=a.diag()['session'];press(a);preview_tap(a)
                    assert not a.diag()['image_viewer']['open'] # During acquisition.
                    finish(a,8);state=a.diag();scan=state['scan_id'];generation=state['scanner']['generation']
                    assert state['scan_result']['status']==1
                    assert state['scan_warning']=='本次漏采8帧，请放大检查是否有缺字、重影或错位。'
                    capture(a,OUT/f'{w}-warning.png')
                    preview_tap(a);t.wait_for(lambda:a.diag()['image_viewer'].get('loaded',False))
                    state=a.diag();assert state['image_viewer']['scan_result'] and not state['scan_released'],state
                    pending=list((root/'.scan-pending').glob('*.png'));assert len(pending)==2
                    original=next(p for p in pending if not p.name.endswith('.preview.png'))
                    import struct
                    assert state['image_viewer']['frame']==list(struct.unpack('>II',original.read_bytes()[16:24]))
                    mtime=original.stat().st_mtime_ns
                    pinch(a,w/2,h/2,scale=3);assert a.diag()['image_viewer']['zoom']>2
                    drag(a,w/2,h/2,-30,-30)
                    capture(a,OUT/f'{w}-original-zoom.png')
                    assert not a.read_messages() and a.diag()['summary']['sessions']==0
                    a.tap(w-38,30);t.wait_for(lambda:not a.diag()['image_viewer']['open'])
                    assert a.diag()['scan_id']==scan and a.diag()['scanner']['generation']==generation
                    assert a.diag()['scan_warning'] and a.diag()['session']==session
                    preview_tap(a);t.wait_for(lambda:a.diag()['image_viewer'].get('loaded',False))
                    assert original.stat().st_mtime_ns==mtime
                    a.send('visibility',visible=False);t.wait_for(lambda:a.diag()['scan_released'])
                    t.wait_for(lambda:not list((root/'.scan-pending').glob('*')))
                    assert not a.diag()['image_viewer']['open'] and not a.read_messages()
                    a.send('visibility',visible=True)
                    press(a);finish(a,9)
                    assert a.diag()['scan_result']['status']!=1 and a.diag()['scan_result']['usable']
                    capture(a,OUT/f'{w}-partial.png');preview_tap(a)
                    t.wait_for(lambda:a.diag()['image_viewer'].get('loaded',False))
                    a.tap(w-38,30);t.wait_for(lambda:not a.diag()['image_viewer']['open'])
                    # Cancel returns to chat without sending or persisting anything.
                    a.tap(w//2,h-36) if h>w else a.tap(w-205,108)
                    t.wait_for(lambda:a.diag()['view']=='chat')
                    t.wait_for(lambda:not list((root/'.scan-pending').glob('*')))
                    press(a);a.send('scanner.test',command='test.press',pressed=False)
                    t.wait_for(lambda:a.diag()['scan_result'].get('type')=='finished')
                    preview_tap(a);assert not a.diag()['image_viewer']['open']
                    capture(a,OUT/f'{w}-empty.png')
                    assert not a.read_messages()
                    if w==960:
                        # Empty retry followed by worker disconnect during review.
                        a.send('scanner.test',command='test.press',pressed=True)
                        t.wait_for(lambda:not a.diag()['scan_result'])
                        finish(a);preview_tap(a)
                        t.wait_for(lambda:a.diag()['image_viewer'].get('loaded',False))
                        os.kill(a.diag()['scanner']['worker_pid'],signal.SIGTERM)
                        t.wait_for(lambda:not a.diag()['image_viewer']['open'])
                        t.wait_for(lambda:a.diag()['scan_released'])
                        assert not list((root/'.scan-pending').glob('*'))
                        assert not a.read_messages()
                finally:a.close()
    finally:
        for k,v in before.items():
            if v is None:os.environ.pop(k,None)
            else:os.environ[k]=v
    print('Three LVGL viewports: warning, normal/partial/empty review, original image, pinch/pan, cached reopen, no save/send, background cancellation and file cleanup passed.')
if __name__=='__main__':run()
