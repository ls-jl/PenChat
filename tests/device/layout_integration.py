#!/usr/bin/env python3
"""Scoped merged-layout acceptance; restores private/public data and never restarts Falcon."""
import argparse,hashlib,json,pathlib,shlex,sqlite3,subprocess,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
APP=json.loads((ROOT/'package.json').read_text())
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);p.add_argument('--package',type=pathlib.Path,required=True);args=p.parse_args()
if args.serial not in ('7G12000010905426','JDA0700006552216','MEB0400008900794'):raise SystemExit('Device not authorized for this merge. Y02/113 remains prohibited.')
s6=args.serial=='JDA0700006552216';target='s6' if s6 else 'orange'
OUT=ROOT/'.work'/('merged-layout-'+args.serial);OUT.mkdir(parents=True,exist_ok=True)
DATA=('/userdisk/miniapp' if s6 else '/userdisk/secondary/miniapp')+'/data/mini_app/pkg/'+APP['appid']+'/data';PUBLIC='/userdisk/Favorite/PenChat'
SAVED=DATA+'.layout-merge-saved';PUBSAVED=PUBLIC+'.layout-merge-saved'
REMOTE='/tmp/penchat-layout-integration.amr';HELPER='/tmp/penchat-layout-tap'
def sh(command):return subprocess.check_output(['adb','-s',args.serial,'shell',command],text=True).replace('\r','').strip()
def push(source,destination):subprocess.run(['adb','-s',args.serial,'push',str(source),destination],check=True,capture_output=True)
def wait(fn,seconds=20):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        try:r=fn()
        except (ValueError,subprocess.CalledProcessError):r=None
        if r:return r
        time.sleep(.12)
    raise RuntimeError('Device transition timed out')
def pid():
    for native in sh('pidof penchat-lvgl || true').split():
        status=sh('cat /proc/'+native+'/stat 2>/dev/null || true').rsplit(')',1)[-1].split()
        if status and status[0] not in ('Z','X'):return native
    return ''
def close():
    sh('miniapp_cli start 8080222437664451');time.sleep(.3)
    native=pid()
    if native:
        if APP['appid'] not in sh("tr '\\0' ' ' < /proc/"+native+'/cmdline'):raise RuntimeError('Unexpected native owner')
        sh('kill -TERM '+native);wait(lambda:not pid(),30)
def state():
    native=pid()
    if not native:return None
    wait(lambda:int(sh("awk '/SigCgt/{print $2}' /proc/"+native+'/status'),16)&(1<<11))
    sh('kill -USR2 '+native)
    def read():
        value=json.loads(sh('cat '+DATA+'/lvgl-diagnostic.json'))
        return value if str(value.get('pid'))==native else None
    return wait(read)
def capture(name):
    image=DATA+'/lvgl-diagnostic.png'
    sh('rm -f '+image)
    sh('kill -USR1 '+pid());wait(lambda:sh('test -s '+image+' && echo yes')=='yes')
    subprocess.run(['adb','-s',args.serial,'pull',DATA+'/lvgl-diagnostic.png',str(OUT/(name+'.png'))],check=True,capture_output=True)
def hashes(path):return sorted(sh('find '+shlex.quote(path)+' -type f -exec sha256sum {} \\;').splitlines())
def raw(x,y):
    rotation=screen.get('tp_direction',screen.get('direction',0))
    rx,ry=(y,W-1-x) if rotation==270 else (W-1-x,H-1-y) if rotation==180 else (H-1-y,x) if rotation==90 else (x,y)
    return rx+screen.get('tp_xoffset',0),ry+screen.get('tp_yoffset',0)
def touch(x,y,hold=None):
    x,y=raw(round(x),round(y));command=HELPER+(' --hold' if hold else '')+' '+str(x)+' '+str(y)
    if hold:command+=' '+str(hold)
    sh('PENCHAT_TOUCH_DEVICE='+event+' '+command+' 2>/dev/null');time.sleep(.15)
def action(name):
    e=state()['editor'];a=e['actions'][name];c=e['coordinate_space']
    touch((a[0]+a[2])*W/(2*c['width']),(a[1]+a[3])*H/(2*c['height']))
def key(name,hold=None):
    e=state()['editor'];k=next(k for k in e['keys'] if k['key']==name);c=e['coordinate_space']
    touch(k['x']*W/c['width'],k['y']*H/c['height'],hold)

screen=json.loads(sh('cat /etc/miniapp/resources/cfg.json'))['screen'];W,H=screen['width'],screen['height']
if s6 and screen.get('direction') in (90,270):W,H=H,W
inputs=sh('cat /proc/bus/input/devices').split('\n\n');event=None
for block in inputs:
    if any(n in block.lower() for n in ['hyn_ts','sitronix']):
        event=next(('/dev/input/'+w for line in block.splitlines() if 'Handlers=' in line for w in line.split('Handlers=',1)[1].split() if w.startswith('event')),None)
if not event:raise RuntimeError('Touchscreen not discovered')
compiler=subprocess.check_output(['bash','-c','source scripts/native-target.sh '+target+'; printf "%s" "$PENCHAT_BUILD_CXX"'],cwd=ROOT,text=True)
helper=OUT/'tap';subprocess.run([compiler,'-std=c++11','-Os',str(ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
push(helper,HELPER);sh('chmod 755 '+HELPER)
host=sh('pidof miniapp');boot=sh('cat /proc/sys/kernel/random/boot_id')
if sh('test ! -e '+SAVED+' && test ! -e '+PUBSAVED+' && echo safe')!='safe':raise RuntimeError('Previous isolation needs recovery')
close();has_data=sh('test -d '+DATA+' && echo yes')=='yes';has_public=sh('test -d '+PUBLIC+' && echo yes')=='yes'
original=hashes(DATA) if has_data else [];public_hashes=hashes(PUBLIC) if has_public else []
private=False;public=False
result={'serial':args.serial,'version':APP['version'],'logical_size':[W,H],'host_unchanged':False,'restored':False}
try:
    if has_data:sh('mv '+DATA+' '+SAVED)
    sh('mkdir -p '+DATA);private=True
    if has_public:sh('mv '+PUBLIC+' '+PUBSAVED)
    sh('mkdir -p '+PUBLIC);public=True
    with tempfile.TemporaryDirectory(prefix='penchat-device-layout-') as temp:
        dbpath=pathlib.Path(temp)/'penchat.db';schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
        with sqlite3.connect(dbpath) as db:
            db.executescript(schema);db.execute("insert into app_settings values('onboarding',?,1)",(json.dumps({'version':1,'complete':True,'step':6}),))
        push(dbpath,DATA+'/penchat.db')
    push(args.package,REMOTE);installed=sh('miniapp_cli install '+REMOTE)
    assert '"ret": 0' in installed,installed
    sh('miniapp_cli start '+APP['appid']);time.sleep(2)
    if not pid():touch(122,H-47)
    wait(pid);d=wait(lambda:state());assert d['width']==W and d['height']==H
    capture('chat');touch(W//2,H-34)
    wait(lambda:(e if (e:=state()['editor'])['active'] and e['keys'] else None),30)
    capture('keyboard');key('language',600);wait(lambda:state()['editor']['layout_menu']);capture('language-popup')
    action('layout:1');key('a');key('l');wait(lambda:state()['editor']['length']==2)
    action('official');wait(lambda:state()['keyboard']);capture('official-keyboard')
    # Return using the current native editor protocol's own cancellation. This
    # does not require external input or enter a real message.
    result['official_open']=True
    sh('miniapp_cli start '+APP['appid']);time.sleep(.5)
    result['native_alive_after_official']=bool(pid())
    result['layout']='compact' if s6 else 'portrait' if H>W else 'wide'
    result['typed_letters']=2
finally:
    close()
    if private:
        sh('rm -rf '+DATA)
        if has_data:sh('mv '+SAVED+' '+DATA)
    if public:
        sh('rm -rf '+PUBLIC)
        if has_public:sh('mv '+PUBSAVED+' '+PUBLIC)
    result['restored']=(not has_data or hashes(DATA)==original) and (not has_public or hashes(PUBLIC)==public_hashes)
    result['host_unchanged']=sh('pidof miniapp')==host and sh('cat /proc/sys/kernel/random/boot_id')==boot
    sh('rm -f '+REMOTE+' '+HELPER)
    (OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    assert result['restored'] and result['host_unchanged'],result
    sh('miniapp_cli start '+APP['appid'])
print(json.dumps(result,ensure_ascii=False))
