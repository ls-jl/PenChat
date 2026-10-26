"""Physical mapped-touch acceptance for owned pinyin, isolated by display_config."""
import json,time,sys

def run(ctx):
    read,tap,wait,capture=map(ctx.get,('state','tap','wait','capture'))
    W,H=ctx['W'],ctx['H']
    sys.path.insert(0,str(ctx['ROOT']/'tests'))
    latencies=[]
    def state():return read()['editor']
    def action(name):
        b=state()['actions'][name];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def key(name):
        k=next(k for k in state()['keys'] if k['key']==name);tap(k['x'],k['y'])
    def open_editor():tap(W//2,H-34);wait(lambda:state()['active'])
    def choose_layout(value):
        k=next(k for k in state()['keys'] if k['key']=='language');x,y=ctx['raw'](k['x'],k['y'])
        ctx['shell']('PENCHAT_TOUCH_DEVICE=/dev/input/'+ctx['event']+' /tmp/penchat-display-tap --hold '+f'{x} {y} 550 2>/dev/null')
        wait(lambda:state()['layout_menu']);capture('pinyin-layout-menu')
        action('layout:'+str({'zh26':0,'en26':1,'zh9':2}[value]));wait(lambda:state()['layout']==value)
    def ready():
        wait(lambda:state()['candidate_count']>0 and not state()['pending'])
        latencies.append(state()['query_ms'])
    def profile():return json.loads(ctx['shell']('cat '+ctx['DATA']+'/ime/user.json'))
    def chat_settings():
        tap(40,32);wait(lambda:read()['navigation_open']);tap(W*2//3-70,H-30);wait(lambda:read()['view']=='settings')
    from settings_ui import locate
    def setting(name):
        b=locate(read,ctx['settings_swipe'],name)['bounds'];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    open_editor();assert state()['layout']=='zh26';capture('pinyin-26')
    for wanted in ['en26','zh9','zh26']:
        key('language');assert state()['layout']==wanted
    for c in 'nihoa':key(c)
    ready();assert state()['corrected_count']>0;capture('pinyin-correction-candidates')
    b=state()['actions'][f"choose:{state()['revision']}:0"];x,y=ctx['raw']((b[0]+b[2])//2,(b[1]+b[3])//2)
    ctx['shell']('PENCHAT_TOUCH_DEVICE=/dev/input/'+ctx['event']+' /tmp/penchat-display-tap --hold '+f'{x} {y} 650 2>/dev/null')
    assert state()['length']==0 and state()['preedit_length']==5;capture('pinyin-correction-explanation')
    key('enter');assert state()['length']==5 and state()['preedit_length']==0
    action('cancel');wait(lambda:not state()['active']);open_editor()
    for c in 'zhogn':key(c)
    ready();key('space');wait(lambda:state()['length']==1 and not state()['preedit_length'])
    capture('pinyin-single-correction');action('cancel');wait(lambda:not state()['active']);open_editor()
    for c in 'nihoa':key(c)
    ready();capture('pinyin-26-candidates');key('space');wait(lambda:state()['preedit_length']==0 and state()['length']==2)
    action('done');wait(lambda:not state()['active']);wait(lambda:ctx['shell']('test -s '+ctx['DATA']+'/ime/user.json && echo yes')=='yes')
    assert any(r['word']=='你好' and r['pinyin']=="ni'hao" for r in profile()['records'])
    open_editor();choose_layout('zh9')
    for c in '64426':key('t9:'+c)
    ready();assert state()['preferred_layout']=='zh9'
    tap(W-30,28);assert state()['expanded'];key('t9:6');assert state()['preedit_length']==6 and not state()['expanded']
    key('delete');ready();assert state()['preedit_length']==5
    capture('pinyin-nine')
    exact=next(k for k in state()['actions'] if k.startswith('exact:'));action(exact);ready();assert state()['preedit_length']==5
    capture('pinyin-nine-filter');key('enter');assert state()['length']==7 and state()['preedit_length']==0
    key('punctuation');capture('pinyin-nine-punctuation');action('punct:，');assert state()['length']==8
    key('language');assert state()['layout']=='en26';key('a');key('language');assert state()['layout']=='zh9'
    time.sleep(2.1);key('language');assert state()['layout']=='en26';time.sleep(2.1);key('language');assert state()['layout']=='zh9'
    action('official');wait(lambda:read()['keyboard']);tap(35,35);wait(lambda:not read()['keyboard']);assert state()['layout']=='zh9' and state()['length']==9
    choose_layout('zh26');key('n');key('i');choose_layout('zh9');key('t9:6');key('t9:4');choose_layout('en26')
    assert state()['parked_count']==2;capture('pinyin-english-parked')
    old=ctx['shell']('sha256sum '+ctx['DATA']+'/ime/user.json').split()[0]
    action('cancel');wait(lambda:not state()['active']);assert ctx['shell']('sha256sum '+ctx['DATA']+'/ime/user.json').split()[0]==old
    chat_settings();setting('keyboard');assert read()['view']=='keyboard_settings';capture('pinyin-settings')
    b=read()['settings']['rows']['pinyin_correction']['bounds'];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    assert not read()['settings']['rows']['pinyin_correction']['checked'];capture('pinyin-correction-disabled')
    b=read()['settings']['rows']['pinyin_correction']['bounds'];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    assert read()['settings']['rows']['pinyin_correction']['checked']
    b=read()['settings']['rows']['pinyin_learning']['bounds'];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    assert not read()['settings']['rows']['pinyin_learning']['checked']
    b=read()['settings']['rows']['clear_pinyin']['bounds'];tap((b[0]+b[2])//2,(b[1]+b[3])//2);wait(lambda:read()['modal']);capture('pinyin-clear-confirm')
    tap(200,H-44);wait(lambda:profile()['records']==[]);tap(W-70,H-44)
    tap(40,32);tap(40,32);wait(lambda:read()['view']=='chat');
    if read()['navigation_open']:tap(W-30,80)
    open_editor();assert state()['layout']=='zh9'
    for c in '64426':key('t9:'+c)
    ready();key('space');wait(lambda:not state()['preedit_length'])
    choose_layout('en26');action('clipboard');assert state()['clipboard_open']
    length=state()['length'];key('x');assert state()['length']==length+1
    tap(W-30,28);assert not state()['clipboard_open']
    action('done');wait(lambda:not state()['active']);assert profile()['records']==[]
    # Repeated editor/OCR transitions do not reopen one decoder per session or leak descriptors.
    fds=lambda:int(ctx['shell']('ls /proc/'+ctx['pid']()+'/fd | wc -l'))
    baseline=fds()
    for _ in range(20):open_editor();action('cancel');wait(lambda:not state()['active'])
    assert fds()<=baseline+1,(baseline,fds())
    ctx['close']();ctx['shell']('miniapp_cli start '+ctx['APP']['appid']);wait(ctx['pid']);wait(lambda:read()['visible']);time.sleep(.4)
    open_editor();assert state()['layout']=='zh9' and state()['parked_count']==0
    assert profile()['records']==[]
    for c in '64':key('t9:'+c)
    before=state()['preedit_length'];p=ctx['pid']()
    ctx['shell']('miniapp_cli start 8080222437664451');wait(lambda:not read()['visible']);assert not state()['ocr_active']
    ctx['shell']('miniapp_cli start '+ctx['APP']['appid']);wait(lambda:read()['visible']);assert ctx['pid']()==p
    assert state()['active'] and state()['preedit_length']==before and state()['top_panel_blocked']
    action('cancel');wait(lambda:not state()['active'])
    ctx['result']['pinyin']={'native_dictionary':True,'typo_candidates_and_long_press':True,'single_character_correction':True,'original_raw_enter':True,'correction_switch':True,'canonical_correction_learning':True,'three_layouts':True,'globe_cycle_and_timeout':True,
        'exact_filter_and_raw_enter':True,'typing_under_right_panels':True,'deferred_learning':True,'clear_disable_restart':True,
        'official_cancel':True,'parked_compositions':True,'query_ms_samples':latencies,'editor_cycles':20,'fd_baseline':baseline,'lifecycle':True}
