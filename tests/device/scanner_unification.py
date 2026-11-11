#!/usr/bin/env python3
"""Scoped scanner platform acceptance; isolates private/public data and never restarts Falcon."""
import argparse,hashlib,json,pathlib,shlex,sqlite3,subprocess,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
APP=json.loads((ROOT/'package.json').read_text())
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);p.add_argument('--package',type=pathlib.Path,required=True)
p.add_argument('--capture-cycles',type=int,choices=range(0,6),default=5)
p.add_argument('--baseline-worker',type=pathlib.Path,help='Diagnostic only: temporarily compare a source-built original scanner, restoring the installed binary afterwards.')
p.add_argument('--ocr-cycles',type=int,choices=range(0,4),default=3);args=p.parse_args()
if args.serial not in ('7G12000010905426','JDA0700006552216','MEB0400008900794','MEA2300009007113'):raise SystemExit('Device not authorized for scanner validation.')
s6=args.serial=='JDA0700006552216';target='cv1826-s' if s6 else 'rk3562'
OUT=ROOT/'.work/scanner-unification/device'/args.serial;OUT.mkdir(parents=True,exist_ok=True)
DATA=('/userdisk/miniapp' if s6 else '/userdisk/secondary/miniapp')+'/data/mini_app/pkg/'+APP['appid']+'/data';PUBLIC='/userdisk/Favorite/PenChat'
SAVED=DATA+'.scan-unification-saved';PUBSAVED=PUBLIC+'.scan-unification-saved'
REMOTE='/tmp/penchat-scan-unification.amr';HELPER='/tmp/penchat-scan-tap'
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
    old=json.loads(sh('cat '+DATA+'/lvgl-diagnostic.json 2>/dev/null || echo {}'))
    previous=old.get('diagnostic_time',-1) if str(old.get('pid'))==native else -1
    sh('kill -USR2 '+native)
    def read():
        value=json.loads(sh('cat '+DATA+'/lvgl-diagnostic.json'))
        return value if str(value.get('pid'))==native and value.get('diagnostic_time',0)>previous else None
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
    e=state()['editor']
    if name=='official' and name not in e['actions']:
        key('language',600);wait(lambda:state()['editor']['layout_menu'])
        e=state()['editor']
    a=e['actions'][name];c=e['coordinate_space']
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
stock_format=sh('v4l2-ctl -d /dev/video23 --get-fmt-video') if not s6 else None
original=hashes(DATA) if has_data else [];public_hashes=hashes(PUBLIC) if has_public else []
private=False;public=False;worker_backup=None;worker_path=None
result={'serial':args.serial,'version':APP['version'],'logical_size':[W,H],'host_unchanged':False,'restored':False,'passed':False,'paper_quality_validated':False}
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
    result['installed']=True
    sh('miniapp_cli start '+APP['appid']);time.sleep(2)
    if not pid():touch(122,H-47)
    wait(pid);d=wait(lambda:state());assert d['width']==W and d['height']==H
    if args.baseline_worker:
        argv=sh("tr '\\0' '\\n' < /proc/"+pid()+'/cmdline').splitlines()
        workspace=argv[argv.index('--workspace')+1]
        assert workspace.startswith(DATA.rsplit('/data',1)[0]+'/')
        worker_path=workspace+'/assets/bin/penchat-scanner'
        backup=worker_path+'.baseline-saved'
        assert sh('test ! -e '+backup+' && echo safe')=='safe'
        close();sh('cp '+worker_path+' '+backup);worker_backup=backup
        push(args.baseline_worker,worker_path+'.baseline-new')
        sh('chmod 755 '+worker_path+'.baseline-new; mv '+worker_path+'.baseline-new '+worker_path)
        result['baseline_worker_sha256']=hashlib.sha256(args.baseline_worker.read_bytes()).hexdigest()
        sh('miniapp_cli start '+APP['appid']);wait(pid)
    expected={'MEB0400008900794':'orange-sc2356','MEA2300009007113':'y02-sc035gs',
              '7G12000010905426':'y07-sc2356','JDA0700006552216':'s6-pag7920'}[args.serial]
    def ready():
        def poll():
            v=state();scan=v.get('scanner',{})
            if scan.get('type') in ('error','fatal'):raise RuntimeError(json.dumps(scan))
            return v if scan.get('type')=='ready' else None
        v=wait(poll,45);scan=v['scanner'];assert scan['profile']==expected
        assert scan['model_ready'] and scan['hardware_armed']
        if not s6:assert scan['runtime_path'].endswith('/lib/librknnrt-rk3562.so'),scan
        return v
    def released():
        wait(lambda:state()['scan_released'],30)
        assert light()=='0'
        wait(lambda:sh('pidof CaptureFrame'),20)
        if stock_format is not None:
            assert sh('v4l2-ctl -d /dev/video23 --get-fmt-video')==stock_format
    def light():
        return sh('cat '+light_path)
    if s6:
        led=json.loads(sh('cat /usr/configs/hal_led_config.json'))['Plum_V0']
        light_path='/sys/class/gpio/gpio'+str(led['white_gpio_number'])+'/value'
    else:
        light_path='/sys/class/leds/flash-led-gpio/brightness' if expected=='y07-sc2356' else '/sys/class/gpio/gpio43/value'
    key_block=next(block for block in inputs if 'Name="led_control"' in block)
    key_node=next('/dev/input/'+word for line in key_block.splitlines() if 'Handlers=' in line
                  for word in line.split('Handlers=',1)[1].split() if word.startswith('event'))
    key_node=next(path for path in sh('find /dev/input/by-path -maxdepth 1 -type l').splitlines()
                  if sh('readlink -f '+shlex.quote(path))==key_node)
    key_helper=OUT/'key';subprocess.run([compiler,'-std=c++11','-Os',str(ROOT/'tests/device/key.cpp'),'-o',str(key_helper)],check=True)
    KEY_HELPER='/tmp/penchat-scan-key';push(key_helper,KEY_HELPER);sh('chmod 755 '+KEY_HELPER)
    def press(ms):
        response=sh(KEY_HELPER+' '+key_node+' 134 1 '+str(ms)+' 100')
        assert 'sent 1 paired events code=134' in response,response
    initial=ready();worker=initial['scanner']['worker_pid'];warmup=initial['scanner']['model_prepared_us']
    result['runtime_path']=initial['scanner'].get('runtime_path','baseline_worker');result['cycles']=[]
    result['profile']=expected
    for cycle in range(args.capture_cycles):
        assert ready()['scanner']['worker_pid']==worker
        press(500)
        r=wait(lambda:(v.get('scan_result') if (v:=state()).get('scan_result',{}).get('type')=='finished' else None),90)
        assert r['profile']==expected and r['counts']['received']>=5,r
        assert r['counts']['discarded_backlog']==0 and r['counts']['spilled']==0,r
        assert light()=='0'
        result['cycles'].append({'cycle':cycle+1,'received':r['counts']['received'],
          'selected':r['counts']['selected'],'dropped':r['counts']['dropped_raw'],'reason':r.get('reason','')})
        print(json.dumps(result['cycles'][-1]),flush=True)
        touch(W//2,H-36) if H>W else touch(W-205,108)
        wait(lambda:state()['view']=='chat');ready()
        touch(40,32);wait(lambda:state()['navigation_open']);released()
        assert str(worker) in sh('pidof penchat-scanner').split()
        touch(W-30,80)
        assert ready()['scanner']['model_prepared_us']==warmup
    result['ocr_cycles']=[]
    for cycle in range(args.ocr_cycles):
        touch(W//2,H-34)
        wait(lambda:(e:=state()['editor'])['active'] and e['ocr_active'] and not e['ocr_preparing'],40)
        released()
        first=state()['editor']['ocr_request'];assert first
        # Physical paired edges must not trigger the system image-preview page.
        press(30)
        wait(lambda:(v:=state())['editor']['active'] and v['visible'] and v['input_focused'] and
             v['touch_input']['active'] and not v['editor']['ocr_busy'],30)
        time.sleep(.6) # Allow the system OCR focus/release notifications to settle.
        action('done');wait(lambda:not state()['editor']['active'],30)
        assert ready()['scanner']['worker_pid']==worker
        result['ocr_cycles'].append({'cycle':cycle+1,'request_id':first})
    if s6:
        # Cancel a warm rearm while CVI may be performing its zero-frame
        # recovery. A late ready must not steal the new editor's OCR lease.
        touch(40,32);wait(lambda:state()['navigation_open']);released()
        touch(W-30,80)
        touch(W//2,H-34)
        wait(lambda:(e:=state()['editor'])['active'] and e['ocr_active'] and not e['ocr_preparing'],40)
        released()
        assert str(worker) in sh('pidof penchat-scanner').split()
        action('done');wait(lambda:not state()['editor']['active'],30)
        assert ready()['scanner']['worker_pid']==worker
        result['preparation_cancel_restore']=True
    sh('miniapp_cli start 8080222437664451');wait(lambda:not state()['visible']);released()
    assert str(worker) in sh('pidof penchat-scanner').split()
    sh('miniapp_cli start '+APP['appid']);wait(lambda:state()['visible']);ready()
    result['background_restore']=True
    sh('kill -KILL '+str(worker));touch(40,32);wait(lambda:state()['navigation_open']);released()
    touch(W-30,80);next_worker=ready()['scanner']['worker_pid'];assert next_worker!=worker
    result['child_crash_restore']=True
    result['passed']=True

except BaseException as error:
    result['error']=str(error)
    try:
        (OUT/'failure.json').write_text(json.dumps(state(),ensure_ascii=False,indent=2)+'\n')
        capture('failure')
    except Exception:
        pass
    raise
finally:
    close()
    if worker_backup:
        sh('mv '+worker_backup+' '+worker_path)
        worker_backup=None
    # The fixture database is now offline and checkpointed. S6 does not ship
    # the sqlite3 CLI; never copy a live database or inspect restored user data.
    if private and result['passed']:
        try:
            snapshot=OUT/'fixture.db'
            subprocess.run(['adb','-s',args.serial,'pull',DATA+'/penchat.db',str(snapshot)],check=True,capture_output=True)
            with sqlite3.connect('file:'+str(snapshot)+'?mode=ro',uri=True) as db:
                assert db.execute('select count(*) from chat_sessions').fetchone()[0]==0
                assert db.execute('select count(*) from chat_messages').fetchone()[0]==0
            result['no_saved_conversation']=True
        except Exception as error:
            result['passed']=False;result['error']=str(error)
    if private:
        sh('rm -rf '+DATA)
        if has_data:sh('mv '+SAVED+' '+DATA)
    if public:
        sh('rm -rf '+PUBLIC)
        if has_public:sh('mv '+PUBSAVED+' '+PUBLIC)
    result['restored']=(not has_data or hashes(DATA)==original) and (not has_public or hashes(PUBLIC)==public_hashes)
    result['host_unchanged']=sh('pidof miniapp')==host and sh('cat /proc/sys/kernel/random/boot_id')==boot
    sh('rm -f '+REMOTE+' '+HELPER+' /tmp/penchat-scan-key')
    (OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    assert result['restored'] and result['host_unchanged'],result
    sh('miniapp_cli start '+APP['appid'])
print(json.dumps(result,ensure_ascii=False))
assert result['passed'],result
