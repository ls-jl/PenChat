"""Locate floating option rows through bounded, text-free diagnostics."""
import time
from test_lvgl_app import wait_for

def select(app,key):
    # Bounds reach their final coordinates before the popup's 150ms entrance
    # animation stops intercepting hits at the previous position.
    time.sleep(.16)
    for _ in range(50):
        state=app.diag()['context_menu'];row=state['choices'][key]['bounds'];v=state['list']
        if row[1]>=v[1] and row[3]<=v[3]:
            app.tap((row[0]+row[2])//2,(row[1]+row[3])//2)
            wait_for(lambda:not app.diag()['modal']);return
        start=v[1]+(v[3]-v[1])*3//4 if row[3]>v[3] else v[1]+(v[3]-v[1])//4
        distance=min(90,(v[3]-v[1])//2)*(-1 if row[3]>v[3] else 1)
        x=(v[0]+v[2])//2
        for i in range(7):
            app.send('touch',x=x,y=start+distance*i//6,pressed=True);time.sleep(.025)
        time.sleep(.1);app.send('touch',x=x,y=start+distance,pressed=False);time.sleep(.15)
    raise AssertionError(('Option not reachable',key,row,v))

def dismiss(app):
    app.tap(2,app.diag()['height']//2)
    wait_for(lambda:not app.diag()['modal'])
