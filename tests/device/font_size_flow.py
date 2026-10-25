"""Real mapped-touch reading scale acceptance inside display_config isolation."""
import time

def run(ctx):
    state,tap,swipe,capture=map(ctx.get,('state','tap','settings_swipe','capture'))
    W,H=ctx['W'],ctx['H']
    from settings_ui import locate
    def box(b):tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def setting(name):box(locate(state,swipe,name)['bounds'])
    def settings():
        assert state()['view']=='chat'
        tap(40,32);assert state()['navigation_open']
        tap(W*2//3-70,H-30);assert state()['view']=='settings'
    def chat():
        while state()['view']!='chat':tap(40,32)
        if state()['navigation_open']:tap(W-30,80)
    def action(title):box(state()['page']['actions'][title])
    def reader_top():
        # A hold-then-drag is deliberately used for Settings; a message reader
        # needs an ordinary swipe so the test does not invoke long-press Copy.
        for _ in range(15):
            if state()['scroll_y']==0:return
            x,y=ctx['raw'](W//2,60);ex,ey=ctx['raw'](W//2,H-40)
            ctx['shell']('PENCHAT_TOUCH_DEVICE=/dev/input/'+ctx['event']+
                         ' /tmp/penchat-display-tap '+f'{x} {y} {ex} {ey} 16 2>/dev/null')
            time.sleep(.4)
        assert state()['scroll_y']==0
    def choose(percent):
        b=state()['reading_font']['slider'];x=b[0]+round((b[2]-b[0])*(percent-75)/75)
        tap(x,(b[1]+b[3])//2)
        ctx['wait'](lambda:state()['reading_font']['draft']==percent)
    settings();setting('font_size');assert state()['view']=='font_size'
    choose(150);capture('font-preview-150')
    action('保存');assert state()['view']=='settings'
    chat();assert state()['reading_font']['size']==32
    reader_top()
    capture('font-chat-150')
    settings();setting('font_size');choose(75);capture('font-preview-75')
    tap(40,32);assert state()['reading_font']['percent']==150
    # Save survives actual process exit and launch, not merely a settings rebuild.
    chat();ctx['close']();ctx['shell']('miniapp_cli start '+ctx['APP']['appid'])
    ctx['wait'](ctx['pid']);ctx['wait'](lambda:state()['visible']);time.sleep(.4)
    assert state()['reading_font']['percent']==150
    settings();setting('archives')
    box(state()['archive_views']['archive'])
    assert state()['view']=='archive_chat' and state()['reading_font']['size']==32
    capture('font-archive-150')
    tap(40,32);tap(40,32);assert state()['view']=='settings'
    setting('font_size');choose(75);action('保存');chat()
    assert state()['reading_font']['size']==16
    capture('font-chat-75')
    settings();setting('font_size');action('恢复默认')
    assert state()['reading_font']['draft']==100
    action('保存');chat();assert state()['reading_font']['size']==21
    ctx['result']['font_size']={'range':[75,150],'touch_slider':True,'cancel':True,
        'restart':True,'archive':True,'reset':True}
