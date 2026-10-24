#!/usr/bin/env python3
"""Explicit-target Falcon geometry acceptance; isolates user data and never restarts Falcon."""
import argparse,json,pathlib,shlex,sqlite3,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
APP=json.loads((ROOT/'package.json').read_text())
parser=argparse.ArgumentParser();parser.add_argument('--serial',required=True)
parser.add_argument('--back-navigation',action='store_true')
parser.add_argument('--history-time',action='store_true')
parser.add_argument('--message-edit',action='store_true')
parser.add_argument('--reply-actions',action='store_true')
args=parser.parse_args();SERIAL=args.serial
assert sum((args.back_navigation,args.history_time,args.message_edit,args.reply_actions))<=1,'Run isolated feature fixtures separately'
DATA='/userdisk/secondary/miniapp/data/mini_app/pkg/'+APP['appid']+'/data'
OUT=ROOT/'.work'/('device-display-'+SERIAL);OUT.mkdir(parents=True,exist_ok=True)
def shell(command):return subprocess.check_output(['adb','-s',SERIAL,'shell',command],text=True).replace('\r','').strip()
def wait(fn,timeout=8):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:value=fn()
        except (json.JSONDecodeError,subprocess.CalledProcessError):value=None
        if value:return value
        time.sleep(.08)
    raise AssertionError('Device transition timed out')
def pid():
    for p in shell('pidof penchat-lvgl || true').split():
        stat=shell('cat /proc/'+p+'/stat 2>/dev/null || true').rsplit(')',1)[-1].split()
        if stat and stat[0] not in ('Z','X'):return p
    return ''
def state(image=False):
    p=pid();assert p
    target=DATA+'/lvgl-diagnostic.json'
    old=shell('cat '+target+' 2>/dev/null || true')
    previous=json.loads(old).get('diagnostic_time',-1) if old else -1
    wait(lambda:int(shell("awk '/SigCgt/{print $2}' /proc/"+p+'/status'),16)&(1<<(9 if image else 11)))
    shell('kill -'+('USR1' if image else 'USR2')+' '+p)
    def fresh():
        value=json.loads(shell('cat '+target))
        return value if str(value.get('pid'))==p and value.get('diagnostic_time',0)>previous else None
    return wait(fresh)
def capture(name):
    state(True)
    subprocess.run(['adb','-s',SERIAL,'pull',DATA+'/lvgl-diagnostic.png',str(OUT/(name+'.png'))],check=True,capture_output=True)
def close():
    shell('miniapp_cli start 8080222437664451');time.sleep(.2)
    p=pid()
    if p:
        assert APP['appid'] in shell("tr '\\0' ' ' < /proc/"+p+'/cmdline')
        shell('kill -TERM '+p)
    wait(lambda:not pid())
def hashes(path):return sorted(shell('find '+shlex.quote(path)+' -type f -exec sha256sum {} \\;').splitlines())
assert shell('uname -m')=='aarch64'
# Only these two boards have user authorization for this acceptance run.
assert SERIAL in ('MEA2300009007113','MEB0400008900794')
screen=json.loads(shell('cat /etc/miniapp/resources/cfg.json'))['screen'];W=screen['width'];H=screen['height']
inputs=shell('cat /proc/bus/input/devices').split('\n\n');event=None
for block in inputs:
    if 'sitronix_ts_spi' in block or 'hyn_ts' in block or 'Hyn_TS' in block:
        candidate=next((word for line in block.splitlines() if 'Handlers=' in line for word in line.split('Handlers=',1)[1].split() if word.startswith('event')),None)
        if candidate:event=candidate
assert event

def raw(x,y):
    assert 0<=x<W and 0<=y<H
    rotation=screen.get('tp_direction',screen.get('direction',0))
    if rotation==270:rx,ry=y,W-1-x
    elif rotation==180:rx,ry=W-1-x,H-1-y
    elif rotation==90:rx,ry=H-1-y,x
    else:rx,ry=x,y
    return rx+screen.get('tp_xoffset',0),ry+screen.get('tp_yoffset',0)
def tap(x,y):
    rx,ry=raw(x,y)
    shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+event+' /tmp/penchat-display-tap '+str(rx)+' '+str(ry)+' 2>/dev/null');time.sleep(.08)
def drag(x,y,ex,ey):
    sx,sy=raw(x,y);tx,ty=raw(ex,ey)
    shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+event+' /tmp/penchat-display-tap --drag-hold '+str(sx)+' '+str(sy)+' '+str(tx)+' '+str(ty)+' 450 200 2>/dev/null')
    time.sleep(.1)
def side_back():drag(20,H//2,W//5,H//2)
def blocked(value):wait(lambda:state()['back_navigation']['policy_ok'] and state()['back_navigation']['blocked']==value)
def settings_swipe(delta):
    y=H-50 if delta<0 else 75;drag(W//2,y,W//2,min(H-10,max(10,y+delta)))
def key(value):
    k=next(k for k in state()['editor']['keys'] if k['key']==value);tap(k['x'],k['y'])
def wake():
    if SERIAL!='MEA2300009007113':return
    for _ in range(2):
        if int(shell('cat /sys/class/backlight/backlight/actual_brightness'))>5:return
        shell('/tmp/penchat-display-key /dev/input/by-path/rk805-pwrkey 116 1 100 300')
        time.sleep(.4)
    assert int(shell('cat /sys/class/backlight/backlight/actual_brightness'))>5,'Y02 did not wake'

if pid():
    s=state();assert not s['generating'] and not s.get('compressing') and not s['title_busy'] and not s['keyboard'] and not s['editor']['active'] and not s['draft_length'] and not s['storage']['unsaved'],'Pending work preserved; finish it before isolated acceptance'
    close()
compiler='/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-g++'
helper=OUT/'tap'
subprocess.run([compiler,'-std=c++11','-Os',str(ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
subprocess.run(['adb','-s',SERIAL,'push',str(helper),'/tmp/penchat-display-tap'],check=True,capture_output=True)
shell('chmod 755 /tmp/penchat-display-tap')
# Wake only when the known Y02 idle state is active; CLI navigation alone does
# not wake its touchscreen. Use ordinary paired power events, never brightness
# settings or a host restart. Do this after the app transition, before touches.
key_helper=OUT/'key'
subprocess.run([compiler,'-std=c++11','-Os',str(ROOT/'tests/device/key.cpp'),'-o',str(key_helper)],check=True)
subprocess.run(['adb','-s',SERIAL,'push',str(key_helper),'/tmp/penchat-display-key'],check=True,capture_output=True)
shell('chmod 755 /tmp/penchat-display-key')
saved=DATA+'.display-acceptance-saved';public='/userdisk/Favorite/PenChat';publicSaved=public+'.display-acceptance-saved'
assert shell('test ! -e '+saved+' && test ! -e '+publicSaved+' && echo safe')=='safe'
hasData=shell('test -d '+DATA+' && echo yes')=='yes';original=hashes(DATA) if hasData else []
hasPublic=shell('test -d '+public+' && echo yes')=='yes';originalPublic=hashes(public) if hasPublic else []
host=shell('pidof miniapp');privateIsolated=publicIsolated=False
messageServer=None;messageHandler=None
result={'serial':SERIAL,'version':APP['version'],'screen':screen,'corners':[]}
try:
    if hasData:shell('mv '+DATA+' '+saved)
    shell('mkdir -p '+DATA);privateIsolated=True
    if hasPublic:shell('mv '+public+' '+publicSaved)
    publicIsolated=True
    if args.reply_actions:
        import message_edit_flow,reply_actions_flow
        sys.path.insert(0,str(ROOT/'tests'))
        from test_reply_actions import fixture as reply_fixture
        messageServer,messageHandler,files=message_edit_flow.prepare(ROOT,OUT/'fixture',DATA,SERIAL,reply_fixture)
        shell('mkdir -p '+DATA+'/assets')
        for source,target in files:subprocess.run(['adb','-s',SERIAL,'push',str(source),target],check=True,capture_output=True)
    if args.message_edit:
        import message_edit_flow
        messageServer,messageHandler,files=message_edit_flow.prepare(ROOT,OUT/'fixture',DATA,SERIAL)
        shell('mkdir -p '+DATA+'/assets')
        for source,target in files:subprocess.run(['adb','-s',SERIAL,'push',str(source),target],check=True,capture_output=True)
    if args.back_navigation or args.history_time:
        sys.path.insert(0,str(ROOT/'tests'))
        from test_image_viewer import fixture
        from test_history_time import seed_history
        import shutil
        sample=OUT/'fixture'
        if sample.exists():shutil.rmtree(sample)
        sample.mkdir()
        if args.history_time:seed_history(sample,1)
        else:fixture(sample)
        files=[(sample/'penchat.db',DATA+'/penchat.db')]
        if args.back_navigation:
            with sqlite3.connect(sample/'penchat.db') as db:
                db.execute('update message_assets set stored_path=?,original_path=?',(DATA+'/assets/original.png',)*2)
            shell('mkdir -p '+DATA+'/assets');files.append((sample/'assets/original.png',DATA+'/assets/original.png'))
        for source,target in files:
            subprocess.run(['adb','-s',SERIAL,'push',str(source),target],check=True,capture_output=True)
    wake()
    shell('miniapp_cli start '+APP['appid']);wait(pid);wait(lambda:state()['visible']);time.sleep(2.2) # Let Falcon's app transition finish before corner mapping.
    if SERIAL=='MEA2300009007113' and int(shell('cat /sys/class/backlight/backlight/actual_brightness'))<=5:
        wake();shell('miniapp_cli start '+APP['appid']);wait(pid);wait(lambda:state()['visible']);time.sleep(2.2)
    s=state();assert (s['width'],s['height'],s['rotation'])==(W,H,screen.get('direction',0));capture('chat')
    # Firmware may swallow the first touch while leaving its idle state. Use
    # a blank-area wake tap, then require subsequent exact mapped events.
    tap(W//2,2);time.sleep(.6)
    for x,y in [(2,2),(W-3,2),(2,H-3),(W-3,H-3)]:
        tap(x,y)
        def mapped():
            p=state()['touch']
            return p if abs(p.get('x',-100)-x)<=1 and abs(p.get('y',-100)-y)<=1 else None
        p=wait(mapped);result['corners'].append([p['x'],p['y']])
    tap(40,32);assert state()['navigation_open'];capture('drawer')
    # Check the pixels captured from the real LVGL page, not just constants.
    pixel_probe=ROOT/'build/host/penchat-icon-pixel-tests'
    subprocess.run([str(pixel_probe),str(OUT/'drawer.png'),str(W*2//3-145),str(H-43),'26','0'],check=True)
    result['gear_pixel_symmetry']=True
    tap(W*2//3-70,H-30);assert state()['view']=='settings';capture('settings')
    assert state()['settings']['viewport'][1:4:2]==[0,H-1]
    # A settled drag reveals Settings controls in the former title-bar area.
    sx,sy=raw(W//2,H-50);ex,ey=raw(W//2,H-170)
    shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+event+' /tmp/penchat-display-tap --drag-hold '+str(sx)+' '+str(sy)+' '+str(ex)+' '+str(ey)+' 450 200 2>/dev/null')
    wait(lambda:not state()['settings']['scrolling'])
    assert state()['settings']['heading'][3]<0;capture('settings-scrolled')
    if args.reply_actions:
        tap(40,32)
        if state()['navigation_open']:tap(W-30,100)
        reply_actions_flow.run(globals(),messageHandler)
        tap(40,32);tap(W*2//3-70,H-30)
    if args.message_edit:
        tap(40,32)
        if state()["navigation_open"]:tap(W-30,100)
        message_edit_flow.run(globals(),messageHandler)
        tap(40,32);tap(W*2//3-70,H-30)
    if args.history_time:
        from settings_ui import activate
        for index,mode in enumerate(('time','title','off')):
            activate(state,tap,settings_swipe,'history_time');wait(lambda:state()['modal']);capture('history-time-picker')
            width=(W-162)//3;tap(100+index*(width+12)+width//2,90);wait(lambda:not state()['modal'])
            tap(40,32);wait(lambda:state()['navigation_open']);s=state()
            assert s['history_captions']['missing']['time']==''
            assert bool(s['history_captions']['today']['time'])==(mode=='time')
            capture('history-'+mode)
            if mode=='time':
                row=next(r for r in s['history_rows'] if r['id']=='today');rx,ry=raw(row['x'],row['y'])
                shell('PENCHAT_TOUCH_DEVICE=/dev/input/'+event+' /tmp/penchat-display-tap --hold '+str(rx)+' '+str(ry)+' 650 2>/dev/null')
                wait(lambda:state()['history_context']=='today');assert state()['history_details']['title_length']>100
                capture('history-full-title');tap(W-10,H-10)
            tap(W*2//3-70,H-30);wait(lambda:state()['view']=='settings')
        result['history_time_modes']=3;result['history_static_title']=True;result['missing_history_time_hidden']=True
    if args.back_navigation:
        native=pid();blocked(True)
        from settings_ui import activate
        activate(state,tap,settings_swipe,'models');wait(lambda:state()['view']=='provider');blocked(True);capture('back-provider')
        side_back();wait(lambda:state()['view']=='settings');assert pid()==native
        side_back();wait(lambda:state()['navigation_open']);blocked(True)
        drag(W*5//6,H//2,W-10,H//2);assert state()['navigation_open']
        result['child_back_and_drawer_tap_only']=True
    else:tap(40,32)
    assert state()['view']=='chat'
    if state()['navigation_open']:tap(W-30,100)
    if args.back_navigation:
        blocked(False)
        for _ in range(8):
            s=state();photo=next((p for p in s['chat_images'] if p['y']>=0 and p['y']+p['height']<=H),None)
            if photo:break
            settings_swipe(100)
        assert photo
        before=state();tap(photo['x']+photo['width']//2,photo['y']+photo['height']//2)
        wait(lambda:state()['image_viewer'].get('loaded'));blocked(True);capture('back-image-fit')
        tap(W-40,114);tap(W-40,114);pan=state()['image_viewer']['pan']
        drag(W//3,H//2,W//3+W//5,H//2)
        assert state()['image_viewer']['open'] and state()['image_viewer']['pan']!=pan
        capture('back-image-pan');side_back();wait(lambda:not state()['image_viewer']['open'])
        assert state()['session']==before['session'] and state()['scroll_y']==before['scroll_y'] and pid()==native
        s=state()
        if s['to_bottom_visible']:tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
        wait(lambda:state()['toolbar_y']==H-58)
        tap(W//2,H-34);wait(lambda:state()['editor']['active']);blocked(True)
        key('language');key('q');assert state()['editor']['length']==1
        side_back();wait(lambda:not state()['editor']['active']);blocked(False)
        assert state()['draft_length']==0 and pid()==native
        result['image_pan_and_edge_back']=result['editor_back_cancels_input']=True
    wait(lambda:state()['toolbar_y']==H-58);time.sleep(.4)
    tap(W//2,H-34);wait(lambda:state()['editor']['active']);capture('keyboard')
    assert state()['editor']['top_panel_blocked']
    actions=state()['editor']['actions']
    assert list(sorted(actions,key=lambda k:actions[k][0]))==['cancel','official','ocr-mode','done','clipboard']
    for name,width in [('official',64),('ocr-mode',48),('done',104),('clipboard',44)]:
        x1,y1,x2,y2=actions[name];assert x2-x1+1==width and y2-y1+1==44
    x1,y1,x2,y2=actions['ocr-mode'];tap((x1+x2)//2,(y1+y2)//2)
    wait(lambda:state()['editor']['ocr_multi']);capture('keyboard-multi')
    tap((x1+x2)//2,(y1+y2)//2);wait(lambda:not state()['editor']['ocr_multi'])
    key('language');key('a');key('b');assert state()['editor']['length']==2
    key('mode');key('1');assert state()['editor']['mode']==1;capture('numbers')
    key('symbols');assert state()['editor']['mode']==2;capture('symbols');key('mode')
    key('language')
    for c in 'nihao':key(c)
    wait(lambda:state()['editor']['candidate_count']>0)
    capture('pinyin');key('space');wait(lambda:state()['editor']['length']>3)
    while state()['editor']['preedit_length']:
        wait(lambda:state()['editor']['candidate_count']>0);key('space')
    result['system_ime_candidates']=True
    tap(W-30,H-30);assert state()['editor']['clipboard_open'];capture('clipboard');tap(W-45,25)
    before=state()['editor'];x1,y1,x2,y2=before['actions']['official'];tap((x1+x2)//2,(y1+y2)//2);wait(lambda:state()['keyboard'])
    shell('miniapp_cli capture /tmp/penchat-display-official.png')
    subprocess.run(['adb','-s',SERIAL,'pull','/tmp/penchat-display-official.png',str(OUT/'official.png')],check=True,capture_output=True)
    tap(35,35);wait(lambda:not state()['keyboard']);assert state()['editor']['length']==before['length']
    wait(lambda:state()['editor']['top_panel_blocked']);tap(W-80,H-30);wait(lambda:not state()['editor']['active'])
    assert state()['draft_length']>0
    result['official_cancel_preserves_input']=True
    p=pid();shell('miniapp_cli start 8080222437664451');wait(lambda:not state()['visible'])
    assert 'allocated by = penchat-lvgl' not in shell('cat /sys/kernel/debug/dri/0/state')
    shell('miniapp_cli start '+APP['appid']);wait(lambda:state()['visible']);assert pid()==p
    assert state()['draft_length']>0;result['background_resume']=True
    if args.back_navigation:
        blocked(False);side_back();wait(lambda:not pid() or not state()['visible'])
        result['root_system_exit']=True
    assert shell('pidof miniapp')==host;result['host_unchanged']=True
except BaseException:
    if pid():
        (OUT/'failure.json').write_text(json.dumps(state(),ensure_ascii=False,indent=2))
        capture('failure')
    raise
finally:
    if messageServer:
        messageServer.shutdown();messageServer.server_close()
    if pid():close()
    if privateIsolated:
        shell('rm -rf '+DATA)
        if hasData:shell('mv '+saved+' '+DATA)
    if publicIsolated:
        shell('rm -rf '+public)
        if hasPublic:shell('mv '+publicSaved+' '+public)
    if hasData:assert hashes(DATA)==original
    if hasPublic:assert hashes(public)==originalPublic
    assert shell('pidof miniapp')==host
    shell('rm -f /tmp/penchat-display-tap /tmp/penchat-display-key /tmp/penchat-display-official.png')
result['user_data_restored']=True
(OUT/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
shell('miniapp_cli start '+APP['appid']);wait(pid);wait(lambda:state()['visible']);capture('final')
print(json.dumps(result,ensure_ascii=False))
