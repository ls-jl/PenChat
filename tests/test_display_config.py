#!/usr/bin/env python3
"""Run the real UI with explicit Falcon configurations, never a device default."""
import json, os, pathlib, signal, struct, subprocess, tempfile, time
import test_lvgl_app as t

BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/display-config';OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def screenshot(app,name,w,h):
    app.process.send_signal(signal.SIGUSR1)
    p=app.directory/'lvgl-diagnostic.png'
    t.wait_for(p.exists)
    data=p.read_bytes()
    assert struct.unpack('>II',data[16:24])==(w,h)
    (OUT/(name+'.png')).write_bytes(data)
    p.unlink()

def run(w,h,rotation):
    with tempfile.TemporaryDirectory(prefix='penchat-display-') as directory:
        a=t.Native(BINARY,pathlib.Path(directory),official_adapter=False,screen={
            'width':w,'height':h,'direction':rotation,'xoffset':0,'yoffset':107 if rotation else 0})
        try:
            boot=a.receive('booted');assert boot['screen']['width']==w and boot['screen']['height']==h
            a.send('display.area',x=0,y=0,width=w,height=h)
            accepted=a.receive('display.ready')
            assert (accepted['width'],accepted['height'],accepted['rotation'])==(w,h,rotation)
            d=a.diag();assert (d['width'],d['height'],d['rotation'])==(w,h,rotation)
            assert d['toolbar_y']==h-58
            screenshot(a,f'{w}-{h}-chat',w,h)
            a.tap(40,32);assert a.diag()['navigation_open']
            a.tap(w-30,60);assert not a.diag()['navigation_open']
            # The input field opens the native editor at every logical resolution.
            a.tap(w//2,h-34);t.wait_for(lambda:a.diag()['editor']['active'])
            d=a.diag()['editor'];keys={k['key']:k for k in d['keys']}
            for key in d['keys']:assert 0<=key['x']<w and 0<=key['y']<h
            for item in d['controls']:
                x1,y1,x2,y2=item['bounds'];assert 0<=x1<=x2<w and 0<=y1<=y2<h, item
            actions=d['actions']
            assert list(sorted(actions,key=lambda k:actions[k][0]))==['cancel','official','ocr-mode','done','clipboard']
            for name,width in [('official',64),('ocr-mode',48),('done',104),('clipboard',44)]:
                x1,y1,x2,y2=actions[name]
                assert x2-x1+1==width and y2-y1+1==44
                assert any(item['bounds']==actions[name] for item in d['controls'])
            assert (actions['done'][1]+actions['done'][3])//2==(actions['official'][1]+actions['official'][3])//2
            screenshot(a,f'{w}-{h}-keyboard',w,h)
            for value in ['q','w','e']:
                k=keys[value];a.tap(k['x'],k['y'])
            t.wait_for(lambda:a.diag()['editor']['preedit_length']==3)
            k=keys['enter'];a.tap(k['x'],k['y'])
            t.wait_for(lambda:a.diag()['editor']['length']==3)
            # Mode transitions, candidates, clipboard and official cancellation use
            # the same logical geometry; they do not rescale already mapped touches.
            k=keys['mode'];a.tap(k['x'],k['y']);assert a.diag()['editor']['mode']==1
            k={v['key']:v for v in a.diag()['editor']['keys']}['symbols'];a.tap(k['x'],k['y']);assert a.diag()['editor']['mode']==2
            a.tap(w-30,h-30);assert a.diag()['editor']['clipboard_open']
            screenshot(a,f'{w}-{h}-clipboard',w,h)
            a.tap(w-45,25);assert not a.diag()['editor']['clipboard_open']
            a.editor_action('official');request=a.receive('keyboard.open')
            assert a.diag()['keyboard']
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            t.wait_for(lambda:not a.diag()['keyboard'])
            a.tap(w-80,h-30);t.wait_for(lambda:not a.diag()['editor']['active'])
            a.tap(40,32);a.tap(w*2//3-70,h-30);assert a.diag()['view']=='settings'
            screenshot(a,f'{w}-{h}-settings',w,h)
            assert a.diag()['settings']['viewport'][1:4:2]==[0,h-1]
            a.settings_swipe(-120);t.wait_for(lambda:not a.diag()['settings']['scrolling'])
            assert a.diag()['settings']['heading'][3]<0
            screenshot(a,f'{w}-{h}-settings-scrolled',w,h)
            for item in a.diag()['settings']['rows'].values():
                x1,y1,x2,y2=item['bounds'];assert 0<=x1<x2<w
        finally:a.close()

if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='penchat-no-config-') as directory:
        root=pathlib.Path(directory)
        result=subprocess.run([str(BINARY),'--headless','--screen-config',str(root/'missing.json'),'--data-dir',str(root)],capture_output=True,text=True)
        assert result.returncode==1 and 'Cannot read Falcon screen configuration' in result.stderr
        assert not (root/'penchat.db').exists()
    for w,h,r in [(960,266,270),(936,280,270),(1024,320,0)]:run(w,h,r)
    print('Falcon configuration: logical display, keyboard, clipboard, official fallback and settings passed at three sizes.')
