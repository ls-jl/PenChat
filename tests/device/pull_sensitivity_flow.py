"""Saved sensitivity and full-release boundary using real, Falcon-mapped touches."""
import time

def run(ctx):
    state,tap,swipe,capture=map(ctx.get,('state','tap','settings_swipe','capture'))
    W,H=ctx['W'],ctx['H']
    from settings_ui import locate
    def settings():
        tap(40,32);assert state()['navigation_open']
        tap(W*2//3-70,H-30);assert state()['view']=='settings'
    def picker():
        b=locate(state,swipe,'pull_sensitivity')['bounds']
        tap((b[0]+b[2])//2,(b[1]+b[3])//2);assert state()['modal']
    def choose(percent):
        d=state()['pull_preferences'];b=d['slider']
        x=b[0]+round((b[2]-b[0])*(percent-d['minimum'])/(d['maximum']-d['minimum']))
        tap(x,(b[1]+b[3])//2);ctx['wait'](lambda:state()['pull_preferences']['draft']==percent)
    def chat():
        tap(40,32)
        if state()['navigation_open']:tap(W-30,80)
        assert state()['view']=='chat'
    def pull(distance):
        sx,sy=ctx['raw'](W//2,H-34);ex,ey=ctx['raw'](W//2,H-34-distance)
        ctx['shell']('PENCHAT_TOUCH_DEVICE=/dev/input/'+ctx['event']+
            ' /tmp/penchat-display-tap '+f'{sx} {sy} {ex} {ey} 18 2>/dev/null')
        time.sleep(.25)
    def full():
        d=state();return d['pull_full_distance']
    # First partial pull freezes the current unobstructed geometry without creating.
    original=state()['session'];pull(50);assert state()['session']==original
    baseline=full();expected=round((142/.85)*(H-58)/208)
    assert abs(baseline-expected)<=1,(baseline,expected)
    settings();picker();before=state()['scroll_y'];capture('pull-baseline')
    choose(10);capture('pull-positive');tap(W-70,H-44)
    assert not state()['modal'] and state()['pull_sensitivity_offset']==0
    assert state()['scroll_y']==before
    picker();choose(-10);capture('pull-negative');tap(W-190,H-44)
    ctx['wait'](lambda:not state()['modal']);assert state()['pull_sensitivity_offset']==-10
    assert state()['scroll_y']==before;chat();pull(50)
    assert full()>baseline and abs(full()*.9-baseline)<=1
    ctx['close']();ctx['shell']('miniapp_cli start '+ctx['APP']['appid'])
    ctx['wait'](ctx['pid']);ctx['wait'](lambda:state()['visible']);time.sleep(.5)
    assert state()['pull_sensitivity_offset']==-10
    settings();picker();assert state()['pull_preferences']['draft']==-10
    tap(167,H-44);assert state()['pull_preferences']['draft']==0;capture('pull-reset')
    tap(W-190,H-44);ctx['wait'](lambda:not state()['modal']);chat();pull(50)
    assert full()==baseline
    # One logical pixel short must not create; a new full release creates once.
    pull(baseline-1);assert state()['session']==original
    pull(baseline);ctx['wait'](lambda:state()['session']!=original)
    blank=state()['session'];capture('pull-created-chat')
    pull(baseline+4);assert state()['session']==blank and not state()['pull_active']
    ctx['result']['pull_sensitivity']={'baseline':baseline,'offset_range':[-25,25],
        'cancel':True,'save':True,'restart':True,'reset':True,
        'full_release_boundary':True,'empty_guard':True,'reader_height':H-58}
