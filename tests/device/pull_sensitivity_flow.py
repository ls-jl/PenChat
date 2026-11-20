"""Saved sensitivity and full-release boundary using real, Falcon-mapped touches."""
import math,time

def run(ctx):
    state,tap,swipe,capture=map(ctx.get,('state','tap','settings_swipe','capture'))
    W,H=ctx['W'],ctx['H']
    portrait=H>W
    from settings_ui import locate
    def settings():
        ctx['open_settings']()
    def picker():
        b=locate(state,swipe,'pull_sensitivity')['bounds']
        tap((b[0]+b[2])//2,(b[1]+b[3])//2);assert state()['modal']
    def choose(percent):
        d=state()['pull_preferences'];b=d['slider']
        x=b[0]+round((b[2]-b[0])*(percent-d['minimum'])/(d['maximum']-d['minimum']))
        tap(x,(b[1]+b[3])//2);ctx['wait'](lambda:state()['pull_preferences']['draft']==percent)
    def chat():
        ctx['ensure_chat']()
    def pull(distance):
        if portrait:
            s=state();b=s['composer_text_bounds'];x=(b[0]+b[2])//2;y=(b[1]+b[3])//2
            assert not s['editor']['active'] and not s['composer_hidden']
        else:x,y=W//2,H-34
        sx,sy=ctx['raw'](x,y);ex,ey=ctx['raw'](x,y-distance)
        ctx['shell']('PENCHAT_TOUCH_DEVICE=/dev/input/'+ctx['event']+
            ' /tmp/penchat-display-tap '+f'{sx} {sy} {ex} {ey} 18 2>/dev/null')
        time.sleep(.25)
    def full():
        d=state();return d['pull_full_distance']
    # First partial pull freezes the current unobstructed geometry without creating.
    original=state()['session'];pull(50);assert state()['session']==original
    baseline=full();geometry=state()
    if portrait:
        reader=geometry['chat_header']['reader_bounds']
        extent=min(geometry['pull_reader_height'],(reader[2]-reader[0]+1)/2)
        expected=int(math.floor((142/.85)*extent/208+.5))*2
    else:expected=round((142/.85)*(H-58)/208)
    assert abs(baseline-expected)<=1,(baseline,expected)
    settings();picker();before=state()['scroll_y'];capture('pull-baseline')
    choose(10);capture('pull-positive');ctx['modal_action'](0,W-70)
    assert not state()['modal'] and state()['pull_sensitivity_offset']==0
    assert state()['scroll_y']==before
    picker();choose(-10);capture('pull-negative');ctx['modal_action'](2,W-190)
    ctx['wait'](lambda:not state()['modal']);assert state()['pull_sensitivity_offset']==-10
    assert state()['scroll_y']==before;chat();pull(50)
    assert full()>baseline
    if portrait:assert full()==int(math.floor((142/(.85*.9))*extent/208+.5))*2
    else:assert abs(full()*.9-baseline)<=1
    ctx['close']();ctx['start_app']()
    ctx['wait'](ctx['pid']);ctx['wait'](lambda:state()['visible']);time.sleep(.5)
    assert state()['pull_sensitivity_offset']==-10
    settings();picker();assert state()['pull_preferences']['draft']==-10
    ctx['modal_action'](1,167);assert state()['pull_preferences']['draft']==0;capture('pull-reset')
    ctx['modal_action'](2,W-190);ctx['wait'](lambda:not state()['modal']);chat();pull(50)
    assert full()==baseline
    # One logical pixel short must not create; a new full release creates once.
    ctx['wait'](lambda:state()['pull_guard_remaining_ms']==0)
    pull(baseline-1);assert state()['session']==original
    ctx['wait'](lambda:state()['pull_guard_remaining_ms']==0)
    pull(baseline);ctx['wait'](lambda:state()['session']!=original)
    blank=state()['session'];capture('pull-created-chat')
    pull(baseline+4);assert state()['session']==blank and not state()['pull_active']
    ctx['result']['pull_sensitivity']={'baseline':baseline,'offset_range':[-25,25],
        'cancel':True,'save':True,'restart':True,'reset':True,
        'full_release_boundary':True,'empty_guard':True,'reader_height':geometry['pull_reader_height'],
        'portrait':portrait,'portrait_extent':extent if portrait else None,'portrait_total_travel_multiplier':2 if portrait else 1}
