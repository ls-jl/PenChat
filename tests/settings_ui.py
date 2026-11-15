"""Locate Settings controls through bounded, text-free layout diagnostics."""
import time

def interactive_viewport(state, bounds):
    viewport=list(bounds)
    scale=state.get('ui_scale',{}).get('applied',100)
    # A visible row can still overlap the system-panel origin band. Locate a
    # center inside the usable reader, without changing app geometry.
    top=max((8*scale+50)//100,min(state.get('width',0),state.get('height',0))//25,
            state.get('safe_insets',[0,0])[1])
    viewport[1]=max(viewport[1],top)
    return viewport

def controls(state):
    assert state['view'] in ('settings','keyboard_settings'), state['view']
    panel=dict(state['settings'])
    if state['view']=='keyboard_settings':
        panel.update(viewport=state['page']['viewport'],scrolling=state['page']['scrolling'])
    return panel

def locate(read, swipe, name):
    for _ in range(30):
        state = read()
        panel=controls(state)
        if panel['scrolling']:
            time.sleep(.12)
            continue
        row = panel['rows'][name]
        x1, y1, x2, y2 = row['bounds']
        viewport = interactive_viewport(state,panel['viewport'])
        if y1 >= panel['viewport'][1] and y2 <= viewport[3] and (y1+y2)//2>=viewport[1]:
            time.sleep(.12)
            stable = controls(read())
            settled = stable['rows'][name]
            if not stable['scrolling'] and settled['bounds']==row['bounds']:
                return settled
            continue
        center = (y1+y2)//2
        swipe(max(-130, min(130, 155-center)))
        time.sleep(.25)
    raise AssertionError('Settings control is not reachable: '+name)

def activate(read, tap, swipe, name, indicator=False):
    row = locate(read, swipe, name)
    x1, y1, x2, y2 = row['indicator' if indicator else 'bounds']
    tap((x1+x2)//2, (y1+y2)//2)
    time.sleep(.1)
    return row
