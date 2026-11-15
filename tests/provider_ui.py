"""Stable Provider page locators shared by focused UI checks."""
import signal,time
import test_lvgl_app as ui
from settings_ui import interactive_viewport


def activate(app,key,tap=True):
    trace=[];scrolls=0;deadline=time.monotonic()+30
    # Waiting for an in-flight fling is not another failed scroll attempt.
    # Keep both a real-gesture budget and an absolute deadline.
    while scrolls<50 and time.monotonic()<deadline:
        state=app.diag();page=state['page'];bounds={**page.get('controls',{}),**page['actions']}[key]
        v=interactive_viewport(state,page['viewport'])
        trace.append((state['scroll_y'],page['scrolling'],bounds,state['editor']['active']))
        if len(trace)>64:trace.pop(0)
        if page['scrolling']:time.sleep(.08);continue
        if bounds[1]>=page['viewport'][1] and bounds[3]<=v[3] and (bounds[1]+bounds[3])//2>=v[1]:
            time.sleep(.12)
            settled=app.diag();settled_page=settled['page']
            current={**settled_page.get('controls',{}),**settled_page['actions']}.get(key)
            if settled_page['scrolling'] or current!=bounds:
                continue
            if tap:
                app.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
                if key=='provider_advanced':time.sleep(.20) # The disclosure reveals its child hit targets over 180ms.
            return bounds
        # Scroll the reader gutter: starting inside an editable field selects text.
        x=v[0]+4;start=v[1]+(v[3]-v[1])*3//4 if bounds[3]>v[3] else v[1]+(v[3]-v[1])//4
        delta=-min(110,(v[3]-v[1])//2) if bounds[3]>v[3] else min(110,(v[3]-v[1])//2)
        scrolls+=1
        for step in range(7):app.send('touch',x=x,y=start+delta*step//6,pressed=True);time.sleep(.02)
        time.sleep(.16) # Settle the reader before release, avoiding test-only fling oscillation.
        app.send('touch',x=x,y=start+delta,pressed=False);time.sleep(.16)
    raise AssertionError(('Unreachable Provider control',key,bounds,v,trace))


def enter(app,value):
    request=app.receive('keyboard.open');app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=value)
    ui.wait_for(lambda:not app.diag()['editor']['active'])


def field(app,key,value):activate(app,'field:'+key);enter(app,value)


def back(app):
    s=app.diag()
    if s['height']>s['width']:
        for _ in range(30):
            if app.diag()['scroll_y']<=0:break
            app.settings_swipe(s['height']//2)
        s=app.diag();scale=s.get('editor',{}).get('requested_scale',100)/100
        app.tap(s['screen_safety']['navigation_x']+round(22*scale),s['page']['heading'][1]+round(17*scale))
    else:app.tap(40,32)


def capture(app,out,name):
    shot=app.directory/'lvgl-diagnostic.png'
    if shot.exists():shot.unlink()
    app.process.send_signal(signal.SIGUSR1);ui.wait_for(shot.exists)
    (out/(name+'.png')).write_bytes(shot.read_bytes())


def open_model(app,model_id,provider_id='p'):
    if not app.diag()['provider_workspace']['active']:
        activate(app,'provider-open:'+provider_id)
    activate(app,'model-edit:'+model_id)


def commit_model(app):
    activate(app,'完成')
    activate(app,'provider-save-bottom')
