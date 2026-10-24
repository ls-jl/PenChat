#!/usr/bin/env python3
"""ORANGE-only real touch/lifecycle acceptance. Never restarts the system host."""
import json, pathlib, subprocess, time, os
ROOT=pathlib.Path(__file__).resolve().parents[2]
APP=json.loads((ROOT/'package.json').read_text());APP_ID=APP['appid']
SERIAL='MEB0400008900794'
DATA='/userdisk/secondary/miniapp/data/mini_app/pkg/'+APP_ID+'/data'
OUT=ROOT/'.work/device-acceptance';OUT.mkdir(parents=True,exist_ok=True)
def adb(command):
    return subprocess.check_output(['adb','-s',SERIAL,'shell',command],text=True).replace('\r','').strip()
def wait(callback,timeout=5):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:
            value=callback()
            if value:return value
        except (subprocess.CalledProcessError,ValueError):pass
        time.sleep(.05)
    raise AssertionError('Device transition timed out')
def pid():
    # Background launchers may reap an exited child only when resumed. A zombie
    # owns no camera/display resources and must not count as a running app.
    for candidate in adb('pidof penchat-lvgl || true').split():
        stat=adb('cat /proc/'+candidate+'/stat 2>/dev/null || true')
        fields=stat.rsplit(')',1)[-1].split() if stat else []
        if fields and fields[0] not in ('Z','X'):return candidate
    return ''
def state(screenshot=False):
    p=pid();assert p
    try:previous=json.loads(adb('cat '+DATA+'/lvgl-diagnostic.json')).get('diagnostic_time',-1)
    except (ValueError,subprocess.CalledProcessError):previous=-1
    wait(lambda:int(adb("awk '/SigCgt/{print $2}' /proc/"+p+'/status'),16)&(1<<(9 if screenshot else 11)))
    adb('kill -'+('USR1' if screenshot else 'USR2')+' '+p)
    time.sleep(.07)
    def current():
        value=json.loads(adb('cat '+DATA+'/lvgl-diagnostic.json'))
        return value if str(value.get('pid'))==p and value.get('generation',0)>0 and value.get('diagnostic_time',previous+1)>previous else None
    return wait(current)
def tap(x,y):
    assert 0<=x<960 and 0<=y<266
    # Verified from Falcon's real pageX/pageY events for ORANGE tp_direction=270.
    adb('/tmp/penchat-ui-tap '+str(y+113)+' '+str(959-x)+' 2>/dev/null')
    time.sleep(.04)
def official_editor():
    wait(lambda:state()['editor']['active'])
    x1,y1,x2,y2=state()['editor']['actions']['official']
    tap((x1+x2)//2,(y1+y2)//2)
    wait(lambda:state()['keyboard'])
def cancel_native_editor():
    wait(lambda:not state()['keyboard'])
    if state()['editor']['active']:tap(630,236)
    wait(lambda:not state()['editor']['active'])
def view(name):return wait(lambda:state()['view']==name)
def to_chat():
    for _ in range(8):
        current=state()
        if current['navigation_open']:tap(800,80)
        elif current['view']=='chat':return
        else:tap(40,32)
    raise AssertionError('Cannot return to chat')
def open_navigation():
    to_chat();tap(40,32);wait(lambda:state()['navigation_open'])
def open_settings():
    open_navigation();tap(570,236);view('settings')
def settings_swipe(delta):
    start=220 if delta<0 else 75
    adb('/tmp/penchat-ui-tap --drag-hold '+str(start+113)+' 459 '+str(start+delta+113)+' 459 450 200 2>/dev/null')
def setting(name,indicator=False):
    import sys
    if str(ROOT/'tests') not in sys.path:sys.path.insert(0,str(ROOT/'tests'))
    from settings_ui import activate
    return activate(state,tap,settings_swipe,name,indicator)
def page_action(title):
    for _ in range(30):
        page=state()['page'];assert page and title in page['actions'],title
        if page['scrolling']:time.sleep(.1);continue
        x1,y1,x2,y2=page['actions'][title];viewport=page['viewport']
        if y1>=viewport[1] and y2<=viewport[3]:
            tap((x1+x2)//2,(y1+y2)//2);return
        settings_swipe(120 if y1<viewport[1] else -120)
    raise AssertionError('Page action is not reachable: '+title)
def history_action(key):
    x1,y1,x2,y2=state()["history_actions"][key]
    tap((x1+x2)//2,(y1+y2)//2)
def home():
    adb('/tmp/penchat-key /dev/input/by-path/adc-keys 102 1 100 300')
    wait(lambda:not pid())
def capture(name):
    state(screenshot=True);subprocess.run(['adb','-s',SERIAL,'pull',DATA+'/lvgl-diagnostic.png',str(OUT/(name+'.png'))],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
if __name__=='__main__':
    assert 'ORANGE' in adb('cat /proc/device-tree/model')
    assert adb('uname -m')=='aarch64'
    sdk=os.environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK')
    toolchain=os.environ.get('JSAPI_TOOLCHAIN_ROOT',sdk+'/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu')
    for name,source in [('penchat-ui-tap','tap.cpp'),('penchat-key','key.cpp')]:
        subprocess.run([toolchain+'/bin/aarch64-linux-gnu-g++','-std=c++11','-Os',str(ROOT/'tests/device'/source),'-o',str(ROOT/'.work'/name)],check=True)
        subprocess.run(['adb','-s',SERIAL,'push',str(ROOT/'.work'/name),'/tmp/'+name],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    adb('chmod 755 /tmp/penchat-key')
    adb('chmod 755 /tmp/penchat-ui-tap')
    adb('miniapp_cli start '+APP_ID);wait(pid)
    to_chat()
    result={'serial':SERIAL,'version':APP['version'],'corners':[],'cycles':[]}
    for x,y in [(2,2),(957,2),(2,263),(957,263)]:
        tap(x,y)
        def matched():
            point=state()['touch']
            return point if 'x' in point and abs(point['x']-x)<=1 and abs(point['y']-y)<=1 else None
        point=wait(matched)
        result['corners'].append([point['x'],point['y']])
    parent=adb('pidof miniapp')
    for cycle in range(20):
        native=pid()
        open_settings();to_chat()
        open_navigation();tap(800,80);view('chat')
        tap(128,232);tap(200,38);view('camera');tap(40,44);view('chat')
        tap(128,232);tap(200,82);view('album');tap(40,44);view('chat')
        assert pid()==native
        home()
        assert 'allocated by = penchat-lvgl' not in adb('cat /sys/kernel/debug/dri/0/state')
        adb('miniapp_cli start '+APP_ID);wait(pid);view('chat')
        assert adb('pidof miniapp')==parent
        p=pid();entry={'cycle':cycle+1,'pid':p,'fds':int(adb('ls /proc/'+p+'/fd | wc -l')),'threads':int(adb('ls /proc/'+p+'/task | wc -l')),'rss_kib':int(adb("awk '/VmRSS/{print $2}' /proc/"+p+'/status'))}
        result['cycles'].append(entry)
        print(json.dumps(entry),flush=True)
    adb('/tmp/penchat-key /dev/input/by-path/adc-keys 102 1 100 300')
    wait(lambda:not pid())
    assert 'allocated by = penchat-lvgl' not in adb('cat /sys/kernel/debug/dri/0/state')
    adb('miniapp_cli start '+APP_ID);wait(pid);view('chat')
    result['physical_home']=True
    tap(500,232);official_editor()
    assert 'allocated by = penchat-lvgl' not in adb('cat /sys/kernel/debug/dri/0/state')
    tap(35,35);cancel_native_editor()
    assert state()['view']=='chat'
    result['ime_cancel']=True
    adb('miniapp_cli start 8080222437664451')
    wait(lambda:not pid() or not state()['visible'])
    assert 'allocated by = penchat-lvgl' not in adb('cat /sys/kernel/debug/dri/0/state')
    adb('miniapp_cli start '+APP_ID);wait(pid);view('chat');wait(lambda:state()['visible'])
    result['background_resume']=True
    crashed=pid();adb('kill -KILL '+crashed);wait(lambda:not pid());time.sleep(.3)
    adb('miniapp_cli capture /tmp/penchat-launcher-error.png')
    subprocess.run(['adb','-s',SERIAL,'pull','/tmp/penchat-launcher-error.png',str(OUT/'launcher-error.png')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    tap(120,220);wait(pid);view('chat');assert pid()!=crashed
    result['crash_retry']=True
    capture('final-chat')
    (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('ORANGE: corner mapping, 20 navigation/camera/reopen cycles, host PID preservation and DRM cleanup passed.')
