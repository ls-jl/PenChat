#!/usr/bin/env python3
"""ORANGE image viewer acceptance using isolated private/public test data."""
import json,shutil,sqlite3,sys,time
import settings_acceptance as isolated
import acceptance as d
from chat_acceptance import push
sys.path.insert(0,str(d.ROOT/'tests'))
from test_image_viewer import fixture as seed

def fixture():
    path=d.ROOT/'.work/device-image-viewer'
    if path.exists():shutil.rmtree(path)
    path.mkdir();seed(path)
    with sqlite3.connect(path/'penchat.db') as db:
        db.execute('update message_assets set original_path=?,stored_path=?',(d.DATA+'/assets/original.png',)*2)
    push(path/'penchat.db',d.DATA+'/penchat.db')
    d.adb('mkdir -p '+d.DATA+'/assets')
    push(path/'assets/original.png',d.DATA+'/assets/original.png')

def open_photo():
    for _ in range(8):
        state=d.state()
        image=next((i for i in state['chat_images'] if i['y']>=0 and i['y']+i['height']<=state['height']),None)
        if image:break
        d.settings_swipe(100);time.sleep(.3)
    else:raise AssertionError('Image is not visible')
    before=d.state()
    d.tap(image['x']+image['width']//2,image['y']+image['height']//2)
    d.wait(lambda:d.state()['image_viewer'].get('loaded'),timeout=10)
    return before

def exercise():
    before=open_photo();state=d.state();w=state['width']
    assert state['image_viewer']['frame']==[1024,768]
    d.capture('image-viewer-fit')
    screen=json.loads(d.adb('cat /etc/miniapp/resources/cfg.json'))['screen'];h=screen['height']
    def raw(x,y):
        rot=screen.get('tp_direction',screen.get('direction',0))
        rx,ry=(y,w-1-x) if rot==270 else (w-1-x,h-1-y) if rot==180 else (h-1-y,x) if rot==90 else (x,y)
        return round(rx+screen.get('tp_xoffset',0)),round(ry+screen.get('tp_yoffset',0))
    def pinch(scale):
        args=[]
        for x,y in [(w/2-50,h/2),(w/2+50,h/2),(w/2-50*scale,h/2),(w/2+50*scale,h/2)]:args.extend(raw(x,y))
        d.adb('/tmp/penchat-ui-tap --pinch '+' '.join(map(str,args))+' 2>/dev/null');time.sleep(.25)
    pinch(4);assert d.state()['image_viewer']['zoom']>3.8
    assert d.state()['image_viewer']['touch_peak']==2
    initial=d.state()['image_viewer']['pan'];p=raw(w//2,h//2);q=raw(w//2-40,h//2-25)
    d.adb('/tmp/penchat-ui-tap --drag-hold '+' '.join(map(str,p+q))+' 450 200 2>/dev/null')
    assert d.state()['image_viewer']['pan']!=initial
    assert not d.state()['pull_active'] and not d.state()['editor']['active']
    d.capture('image-viewer-zoom-pan')
    pinch(.12);d.wait(lambda:d.state()['image_viewer']['zoom']==1)
    # Keep the inter-tap interval on-device; two adb round trips can exceed
    # the double-tap window even though both individual taps are valid.
    d.adb('for i in 1 2; do /tmp/penchat-ui-tap 243 559 2>/dev/null; done')
    assert d.state()['image_viewer']['zoom']==2
    native=d.pid()
    d.adb('miniapp_cli start 8080222437664451')
    d.wait(lambda:not d.state()['visible'])
    assert 'allocated by = penchat-lvgl' not in d.adb('cat /sys/kernel/debug/dri/0/state')
    d.adb('miniapp_cli start '+d.APP_ID);d.wait(lambda:d.state()['visible']);time.sleep(.6)
    assert d.pid()==native and d.state()['image_viewer']['zoom']==2
    d.tap(w-38,30);d.wait(lambda:not d.state()['image_viewer']['open'])
    after=d.state()
    assert after['session']==before['session'] and after['scroll_y']==before['scroll_y']
    for _ in range(10):
        open_photo();d.tap(w-38,30);d.wait(lambda:not d.state()['image_viewer']['open'])
    assert not d.state()['generating'] and d.pid()==native
    d.capture('image-viewer-returned-chat')

if __name__=='__main__':
    isolated.fixture=fixture;isolated.exercise=exercise
    isolated.main({'image_original':True,'zoom_pan_double_tap':True,'reopen_cycles':10,
                   'lifecycle':True,'scroll_restored':True,'user_files_restored':True,'host_unchanged':True})
