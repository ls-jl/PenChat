"""Locate Settings controls through bounded, text-free layout diagnostics."""
import time

def locate(read, swipe, name):
    for _ in range(30):
        state = read()
        assert state['view'] == 'settings', state['view']
        if state['settings']['scrolling']:
            time.sleep(.12)
            continue
        row = state['settings']['rows'][name]
        x1, y1, x2, y2 = row['bounds']
        viewport = state['settings']['viewport']
        if y1 >= viewport[1] and y2 <= viewport[3]:
            time.sleep(.12)
            stable = read()['settings']
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
