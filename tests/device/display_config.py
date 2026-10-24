#!/usr/bin/env python3
"""Explicit-target Falcon geometry acceptance; isolates user data and never restarts Falcon."""
import argparse,json,pathlib,shlex,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
APP=json.loads((ROOT/'package.json').read_text())
parser=argparse.ArgumentParser();parser.add_argument('--serial',required=True)
args=parser.parse_args();SERIAL=args.serial
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
def key(value):
    k=next(k for k in state()['editor']['keys'] if k['key']==value);tap(k['x'],k['y'])

if pid():
    s=state();assert not s['generating'] and not s.get('compressing') and not s['title_busy'] and not s['keyboard'] and not s['editor']['active'] and not s['draft_length'] and not s['storage']['unsaved'],'Pending work preserved; finish it before isolated acceptance'
    close()
compiler='/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-g++'
helper=OUT/'tap'
subprocess.run([compiler,'-std=c++11','-Os',str(ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
subprocess.run(['adb','-s',SERIAL,'push',str(helper),'/tmp/penchat-display-tap'],check=True,capture_output=True)
shell('chmod 755 /tmp/penchat-display-tap')
saved=DATA+'.display-acceptance-saved';public='/userdisk/Favorite/PenChat';publicSaved=public+'.display-acceptance-saved'
assert shell('test ! -e '+saved+' && test ! -e '+publicSaved+' && echo safe')=='safe'
hasData=shell('test -d '+DATA+' && echo yes')=='yes';original=hashes(DATA) if hasData else []
hasPublic=shell('test -d '+public+' && echo yes')=='yes';originalPublic=hashes(public) if hasPublic else []
host=shell('pidof miniapp');privateIsolated=publicIsolated=False
result={'serial':SERIAL,'version':APP['version'],'screen':screen,'corners':[]}
try:
    if hasData:shell('mv '+DATA+' '+saved)
    shell('mkdir -p '+DATA);privateIsolated=True
    if hasPublic:shell('mv '+public+' '+publicSaved)
    publicIsolated=True
    shell('miniapp_cli start '+APP['appid']);wait(pid);wait(lambda:state()['visible']);time.sleep(2.2) # Let Falcon's app transition finish before corner mapping.
    s=state();assert (s['width'],s['height'],s['rotation'])==(W,H,screen.get('direction',0));capture('chat')
    for x,y in [(2,2),(W-3,2),(2,H-3),(W-3,H-3)]:
        tap(x,y)
        def mapped():
            p=state()['touch']
            return p if abs(p.get('x',-100)-x)<=1 and abs(p.get('y',-100)-y)<=1 else None
        p=wait(mapped);result['corners'].append([p['x'],p['y']])
    tap(40,32);assert state()['navigation_open'];capture('drawer')
    tap(W*2//3-70,H-30);assert state()['view']=='settings';capture('settings')
    tap(40,32);assert state()['view']=='chat'
    if state()['navigation_open']:tap(W-30,100)
    wait(lambda:state()['toolbar_y']==H-58);time.sleep(.4)
    tap(W//2,H-34);wait(lambda:state()['editor']['active']);capture('keyboard')
    assert state()['editor']['top_panel_blocked']
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
    before=state()['editor'];tap(W-126,H-30);wait(lambda:state()['keyboard'])
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
    assert shell('pidof miniapp')==host;result['host_unchanged']=True
except BaseException:
    if pid():
        (OUT/'failure.json').write_text(json.dumps(state(),ensure_ascii=False,indent=2))
        capture('failure')
    raise
finally:
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
    shell('rm -f /tmp/penchat-display-tap /tmp/penchat-display-official.png')
result['user_data_restored']=True
(OUT/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
shell('miniapp_cli start '+APP['appid']);wait(pid);wait(lambda:state()['visible']);capture('final')
print(json.dumps(result,ensure_ascii=False))
