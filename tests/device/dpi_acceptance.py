#!/usr/bin/env python3
"""DPI completion acceptance; restores private/public data and never restarts Falcon."""
import argparse,hashlib,json,pathlib,shlex,sqlite3,subprocess,tempfile,time,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
APP=json.loads((ROOT/'package.json').read_text())
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);p.add_argument('--package',type=pathlib.Path,required=True);args=p.parse_args()
if args.serial not in ('7G12000010905426','JDA0700006552216','MEB0400008900794','MEA2300009007113'):raise SystemExit('Device is not in the explicit acceptance allowlist.')
s6=args.serial=='JDA0700006552216';target='cv1826-s' if s6 else 'rk3562'
OUT=ROOT/'.work'/('dpi-device-'+args.serial);OUT.mkdir(parents=True,exist_ok=True)
DATA=('/userdisk/miniapp' if s6 else '/userdisk/secondary/miniapp')+'/data/mini_app/pkg/'+APP['appid']+'/data';PUBLIC='/userdisk/Favorite/PenChat'
SAVED=DATA+'.dpi-acceptance-saved';PUBSAVED=PUBLIC+'.dpi-acceptance-saved'
REMOTE='/tmp/penchat-dpi-acceptance.amr';HELPER='/tmp/penchat-dpi-tap'
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
    try:previous=json.loads(sh('cat '+DATA+'/lvgl-diagnostic.json')).get('diagnostic_time',-1)
    except (ValueError,subprocess.CalledProcessError):previous=-1
    sh('kill -USR2 '+native)
    def read():
        value=json.loads(sh('cat '+DATA+'/lvgl-diagnostic.json'))
        return value if str(value.get('pid'))==native and value.get('diagnostic_time',-1)>previous else None
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

def tap_bounds(bounds):touch((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
def reveal(bounds):
    for _ in range(40):
        d=state();b=bounds();v=d['page']['viewport']
        if b[1]>=v[1] and b[3]<=v[3]:return
        x=(v[0]+v[2])//2;start=v[1]+(v[3]-v[1])*(3 if b[3]>v[3] else 1)//4
        end=start+(-1 if b[3]>v[3] else 1)*min(100,(v[3]-v[1])//2)
        sx,sy=raw(x,start);ex,ey=raw(x,end)
        sh('PENCHAT_TOUCH_DEVICE='+event+' '+HELPER+' '+str(sx)+' '+str(sy)+' '+str(ex)+' '+str(ey)+' 2>/dev/null')
        time.sleep(.35)
    raise RuntimeError('Control cannot be reached')
def page_action(name):
    def bounds():
        p=state()['page'];return {**p.get('controls',{}),**p['actions']}[name]
    reveal(bounds);tap_bounds(bounds())

screen=json.loads(sh('cat /etc/miniapp/resources/cfg.json'))['screen'];W,H=screen['width'],screen['height']
if s6 and screen.get('direction') in (90,270):W,H=H,W
compiler=subprocess.check_output(['bash','-c','source scripts/native-target.sh '+target+'; printf "%s" "$PENCHAT_BUILD_CXX"'],cwd=ROOT,text=True)
helper=OUT/'tap';subprocess.run([compiler,'-std=c++11','-Os',str(ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
push(helper,HELPER);sh('chmod 755 '+HELPER)
event=sh(HELPER+' --discover')
assert event.startswith('/dev/input/event') and event.rsplit('event',1)[1].isdigit()
if pid():
    d=state();assert not d['generating'] and not d.get('compressing') and not d['keyboard'] and not d['editor']['active'] and not d['draft_length'] and not d['storage']['unsaved'] and not d.get('provider_workspace',{}).get('dirty'), 'Pending input/work preserved; acceptance deferred'
host=sh('pidof miniapp');boot=sh('cat /proc/sys/kernel/random/boot_id')
if sh('test ! -e '+SAVED+' && test ! -e '+PUBSAVED+' && echo safe')!='safe':raise RuntimeError('Previous isolation needs recovery')
has_data=sh('test -d '+DATA+' && echo yes')=='yes';has_public=sh('test -d '+PUBLIC+' && echo yes')=='yes'
original=[];public_hashes=hashes(PUBLIC) if has_public else []
private=False;public=False
result={'serial':args.serial,'version':APP['version'],'logical_size':[W,H],'host_unchanged':False,'restored':False}
try:
    # Shield the original public exports from normal shutdown cleanup.
    if has_public:sh('mv '+PUBLIC+' '+PUBSAVED)
    sh('mkdir -p '+PUBLIC);public=True
    close()
    original=hashes(DATA) if has_data else []
    if has_data:sh('mv '+DATA+' '+SAVED)
    sh('mkdir -p '+DATA);private=True
    with tempfile.TemporaryDirectory(prefix='penchat-device-layout-') as temp:
        dbpath=pathlib.Path(temp)/'penchat.db';schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
        with sqlite3.connect(dbpath) as db:
            db.executescript(schema);db.execute("insert into app_settings values('onboarding',?,1)",(json.dumps({'version':1,'complete':True,'step':6}),))
            db.execute("insert into providers(id,name,type,base_url,api_key,created_at,updated_at) values('p','DPI fixture','openai-compatible','http://127.0.0.1:1/v1','test-only-fake-key',1,1)")
            db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,is_default,created_at,updated_at) values('a','p','fixture','DPI fixture','chat',1,1,1)")
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at,default_provider_id,default_model_config_id) values('dpi-chat','DPI acceptance','active',1,1,1,'p','a')")
            db.execute("insert into app_settings values('active_session_id','\"dpi-chat\"',1)")
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('dpi-msg','dpi-chat',1,'assistant','complete','text',?,1,1)",('# DPI 字号与布局\n\n**中文 English** 与 $x^2+1$。\n\n| 字段 | 内容 |\n|---|---|\n| A | 布局与选择 |\n\n'+('阅读和滚动测试。\n\n'*12),))
            db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        push(dbpath,DATA+'/penchat.db')
    push(args.package,REMOTE);installed=sh('miniapp_cli install '+REMOTE)
    assert '"ret": 0' in installed,installed
    sh('miniapp_cli start '+APP['appid']);time.sleep(2)
    if not pid():touch(122,H-47)
    wait(pid);d=wait(lambda:state());assert d['width']==W and d['height']==H
    capture('chat')
    # Drawers and menus use current diagnostic bounds; no resolution literals.
    d=state();touch(d['screen_safety']['navigation_x']+24 if H>W else 40,d['safe_insets'][1]+24 if H>W else 32)
    wait(lambda:state()['navigation_open']);capture('drawer')
    row=state()['history_rows'][0];touch(row['x'],row['y'],650)
    wait(lambda:state()['history_context']);capture('history-menu')
    touch(W-12,H//2);wait(lambda:not state()['modal'])
    tap_bounds(state()['navigation_footer']['settings']['bounds'])
    wait(lambda:state()['view']=='settings');capture('settings')
    assert len(state()['settings']['rows'])==23
    reveal(lambda:state()['settings']['rows']['models']['bounds'])
    tap_bounds(state()['settings']['rows']['models']['bounds'])
    wait(lambda:state()['view']=='provider')
    page_action('provider-open:p');capture('provider')
    page_action('field:name')
    wait(lambda:(e if (e:=state()['editor'])['active'] and e['keys'] else None),30)
    e=state()['editor'];assert e['requested_scale']==100 and e['effective_scale']==100
    capture('keyboard')
    key('n');key('i');wait(lambda:state()['editor']['candidate_count']>0)
    capture('candidates');result['pinyin_candidates']=True
    if 'expand' in state()['editor']['actions']:
        action('expand');wait(lambda:state()['editor']['expanded']);capture('candidates-expanded');action('expand')
        result['candidate_expansion']=True
    key('enter');wait(lambda:state()['editor']['preedit_length']==0)
    action('cancel');wait(lambda:not state()['editor']['active'])
    page_action('field:name');wait(lambda:state()['editor']['active'] and state()['editor']['keys'])
    key('language',600);wait(lambda:state()['editor']['layout_menu']);capture('language-popup')
    action('layout:1')
    old=state()['editor']['length'];key('a');key('l');wait(lambda:state()['editor']['length']==old+2)
    e=state()['editor'];letters={k['key']:k for k in e['keys']};space=e['coordinate_space']
    points=[raw(round(letters[k]['x']*W/space['width']),round(letters[k]['y']*H/space['height'])) for k in ('a','l')]
    values=[*points[0],*points[1],*points[0],*points[1]]
    sh('PENCHAT_TOUCH_DEVICE='+event+' '+HELPER+' --pinch '+' '.join(map(str,values))+' 2>/dev/null')
    wait(lambda:state()['editor']['length']==old+4);result['two_contact_rollover']=True
    capture('typed-field')
    e=state()['editor'];b=e['text_bounds'];space=e['coordinate_space']
    touch((b[0]+12)*W/space['width'],(b[1]+10)*H/space['height'],650)
    wait(lambda:state()['editor']['selected']);capture('selection');result['text_selection']=True
    action('official');wait(lambda:state()['keyboard']);capture('official-keyboard')
    # Return using the current native editor protocol's own cancellation. This
    # does not require external input or enter a real message.
    result['official_open']=True
    sh('miniapp_cli start '+APP['appid']);time.sleep(.5)
    result['native_alive_after_official']=bool(pid())
    result['layout']='compact' if s6 else 'portrait' if H>W else 'wide'
    result['typed_letters']=4
    result['settings_rows']=23
    result['history_menu']=True
    result['provider_field']=True
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
# Verify the executable actually running after restoring the original data.
wait(pid)
d=state()
with zipfile.ZipFile(args.package) as package:
    expected=hashlib.sha256(package.read('assets/bin/penchat-lvgl')).hexdigest()
actual=sh('sha256sum /proc/'+pid()+'/exe').split()[0]
assert actual==expected, 'Live executable does not match the installed payload'
assert d['editor']['requested_scale']==100 and d['editor']['effective_scale']==100
result.update(native_ready_after_restore=True,native_sha256=actual,production_scale=100)
(OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False))
