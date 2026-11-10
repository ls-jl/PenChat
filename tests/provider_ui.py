"""Stable Provider page locators shared by focused UI checks."""
import signal,time
import test_lvgl_app as ui


def activate(app,key,tap=True):
    for _ in range(50):
        state=app.diag();page=state['page'];bounds={**page.get('controls',{}),**page['actions']}[key]
        v=page['viewport']
        if page['scrolling']:time.sleep(.08);continue
        if bounds[1]>=v[1] and bounds[3]<=v[3]:
            if tap:app.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
            return bounds
        x=(v[0]+v[2])//2;start=v[1]+(v[3]-v[1])*3//4 if bounds[3]>v[3] else v[1]+(v[3]-v[1])//4
        delta=-min(110,(v[3]-v[1])//2) if bounds[3]>v[3] else min(110,(v[3]-v[1])//2)
        for step in range(7):app.send('touch',x=x,y=start+delta*step//6,pressed=True);time.sleep(.02)
        app.send('touch',x=x,y=start+delta,pressed=False);time.sleep(.12)
    raise AssertionError(('Unreachable Provider control',key,bounds,v))


def enter(app,value):
    request=app.receive('keyboard.open');app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=value)
    ui.wait_for(lambda:not app.diag()['editor']['active'])


def field(app,key,value):activate(app,'field:'+key);enter(app,value)


def back(app):
    s=app.diag();app.tap(48,48) if s['height']>s['width'] else app.tap(40,32)


def capture(app,out,name):
    shot=app.directory/'lvgl-diagnostic.png'
    if shot.exists():shot.unlink()
    app.process.send_signal(signal.SIGUSR1);ui.wait_for(shot.exists)
    (out/(name+'.png')).write_bytes(shot.read_bytes())
