#!/usr/bin/env python3
"""DPI completion acceptance; restores private/public data and never restarts Falcon."""
import argparse,hashlib,json,pathlib,shlex,signal,sqlite3,subprocess,tempfile,time,zipfile
from release_lifecycle import install_termination_handler,defer_cleanup_signals
ROOT=pathlib.Path(__file__).resolve().parents[2]
APP=json.loads((ROOT/'package.json').read_text())
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);p.add_argument('--package',type=pathlib.Path,required=True);p.add_argument('--scale-settings',action='store_true');p.add_argument('--theme-settings',action='store_true');args=p.parse_args()
if args.serial not in ('7G12000010905426','JDA0700006552216','MEB0400008900794','MEA2300009007113'):raise SystemExit('Device is not in the explicit acceptance allowlist.')
s6=args.serial=='JDA0700006552216';target='cv1826-s' if s6 else 'rk3562'
OUT=ROOT/'.work'/'ui-unification'/('device-'+args.serial);OUT.mkdir(parents=True,exist_ok=True)
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
    from release_lifecycle import close_app
    close_app(args.serial, APP['appid'], sh, pid, state)
def state():
    native=pid()
    if not native:return None
    wait(lambda:int(sh("awk '/SigCgt/{print $2}' /proc/"+native+'/status'),16)&(1<<11))
    try:
        old=json.loads(sh('cat '+DATA+'/lvgl-diagnostic.json'))
        previous=old.get('diagnostic_time',-1) if str(old.get('pid'))==native else -1
    except (ValueError,subprocess.CalledProcessError):previous=-1
    sh('kill -USR2 '+native)
    def read():
        value=json.loads(sh('cat '+DATA+'/lvgl-diagnostic.json'))
        return value if str(value.get('pid'))==native and value.get('diagnostic_time',-1)>previous else None
    return wait(read)
def foreground_ready():
    d=state()
    return d if d and d.get('visible') and d.get('page_visible',True) and d.get('window_focused') and d.get('input_focused') and d.get('touch_input',{}).get('active') else None
def stable_foreground():
    deadline=time.monotonic()+20;since=None
    while time.monotonic()<deadline:
        d=foreground_ready()
        if not d:since=None
        elif since is None:since=time.monotonic()
        elif time.monotonic()-since>=.6:return d
        time.sleep(.1)
    raise RuntimeError('Foreground/native input did not remain ready')
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
    from touch_ack import retry_unseen_tap
    logical=[round(x),round(y)];rx,ry=raw(*logical)
    command=HELPER+(' --hold' if hold else '')+' '+str(rx)+' '+str(ry)
    if hold:command+=' '+str(hold)
    for attempt in range(2):
        before=stable_foreground() if pid() else None
        receipt=sh('PENCHAT_TOUCH_DEVICE='+event+' '+command+' 2>&1\npenchat_dpi_touch_status=$?\nprintf "\\nstatus=%s\\n" "$penchat_dpi_touch_status"')
        assert receipt.endswith('status=0'),receipt
        time.sleep(.15);after=state() if pid() else None
        result.setdefault('touch_trace',[]).append({'logical':logical,'raw':[rx,ry],'receipt':receipt,
            'pid_before':None if before is None else before['pid'],
            'touch_before':None if before is None else before.get('touch'),
            'touch_after':None if after is None else after.get('touch'),
            'input_active_before':None if before is None else before['touch_input']['active'],'attempt':attempt})
        result['touch_trace']=result['touch_trace'][-64:]
        if before is None or after is None or after['pid']!=before['pid']:return
        def acknowledged():
            current=state()
            return current if current and current.get('touch',{}).get('gesture')!=before.get('touch',{}).get('gesture') and not current.get('touch',{}).get('pressed',False) else None
        try:wait(acknowledged,seconds=1.2);return
        except RuntimeError:
            current=state()
            if attempt or not retry_unseen_tap(before,current,allow_cancelled_down=True):raise
            probe='/tmp/penchat-release-input-state'
            if sh('test -x '+probe+' && echo yes')!='yes':raise
            kernel=json.loads(sh(probe+' '+event))
            if kernel.get('error') or kernel.get('btn_touch') or kernel.get('active_slots'):raise
            sh(probe+' '+event+' --release');current=stable_foreground()
            if not retry_unseen_tap(before,current,allow_cancelled_down=True):raise
            result['unobserved_input_retries']=result.get('unobserved_input_retries',0)+1

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
        if d['page'].get('scrolling'):time.sleep(.12);continue
        top=max(v[1],d['safe_insets'][1] if H>W else v[1])
        if b[1]>=top and b[3]<=v[3]:
            time.sleep(.12)
            if bounds()==b:return
            continue
        x=(v[0]+v[2])//2;start=v[1]+(v[3]-v[1])*(3 if b[3]>v[3] else 1)//4
        end=start+(-1 if b[3]>v[3] else 1)*min(100,(v[3]-v[1])//2)
        sx,sy=raw(x,start);ex,ey=raw(x,end)
        assert foreground_ready(),'DPI reveal drag stopped after foreground loss'
        sh('PENCHAT_TOUCH_DEVICE='+event+' '+HELPER+' '+str(sx)+' '+str(sy)+' '+str(ex)+' '+str(ey)+' 2>/dev/null')
        time.sleep(.35)
    raise RuntimeError('Control cannot be reached')
def page_action(name):
    def bounds():
        p=state()['page'];return {**p.get('controls',{}),**p['actions']}[name]
    reveal(bounds);tap_bounds(bounds())

def drag(x,y,ex,ey):
    assert foreground_ready(),'DPI drag stopped after foreground loss'
    sx,sy=raw(round(x),round(y));ex,ey=raw(round(ex),round(ey))
    sh('PENCHAT_TOUCH_DEVICE='+event+' '+HELPER+' '+str(sx)+' '+str(sy)+' '+str(ex)+' '+str(ey)+' 2>/dev/null')
    time.sleep(.45)
def open_settings():
    d=state();percent=d['ui_scale']['applied'];dp=lambda n:(n*percent+50)//100
    x=d['screen_safety']['navigation_x'] if H>W else dp(16)
    y=d['safe_insets'][1] if H>W else dp(8)
    touch(x+max(44,dp(48))//2,y+max(44,dp(48))//2)
    wait(lambda:state()['navigation_open'])
    tap_bounds(state()['navigation_footer']['settings']['bounds']);wait(lambda:state()['view']=='settings')
def apply_theme(mode):
    reveal(lambda:state()['settings']['rows']['theme']['bounds'])
    tap_bounds(state()['settings']['rows']['theme']['bounds'])
    wait(lambda:state()['context_menu']['kind']=='choice:theme')
    tap_bounds(state()['context_menu']['choices'][mode]['bounds'])
    def applied():
        d=state();return d if d['appearance']['theme']==mode and not d['appearance']['pending'] and not d['modal'] else None
    wait(applied)
def reader_bounds(d):
    b=list(d['chat_header']['reader_bounds'])
    c=d.get('composer_bounds',[])
    if c and c[1]>b[1]:b[3]=min(b[3],c[1]-8)
    return b
def capture_markdown(prefix):
    # A restarted native app opens the isolated chat. Capture overlapping
    # viewports so compact/150% screens still expose the table and formula.
    for _ in range(40):
        d=state()
        if d['scroll_y']<=0:break
        b=reader_bounds(d);x=(b[0]+b[2])//2
        drag(x,b[1]+(b[3]-b[1])*.2,x,b[1]+(b[3]-b[1])*.8)
    else:raise RuntimeError('Markdown top cannot be reached')
    capture(prefix+'-markdown-top')
    for index in range(1,7):
        d=state()
        if d['scroll_bottom']<=0:break
        b=reader_bounds(d);x=(b[0]+b[2])//2
        drag(x,b[1]+(b[3]-b[1])*.8,x,b[1]+(b[3]-b[1])*.2)
        capture(prefix+'-markdown-'+str(index))

install_termination_handler()

screen=json.loads(sh('cat /etc/miniapp/resources/cfg.json'))['screen'];W,H=screen['width'],screen['height']
if s6 and screen.get('direction') in (90,270):W,H=H,W
original_scale=None;original_theme=None
if pid():
    d=state();original_scale=d.get('ui_scale',{}).get('applied',100);original_theme=d.get('appearance',{}).get('theme','dark');assert not d['generating'] and not d.get('compressing') and not d['keyboard'] and not d['editor']['active'] and not d['draft_length'] and not d['storage']['unsaved'] and not d.get('ui_scale',{}).get('pending') and not d.get('provider_workspace',{}).get('dirty') and not d.get('appearance',{}).get('pending'), 'Pending input/work preserved; acceptance deferred'
compiler=subprocess.check_output(['bash','-c','source scripts/native-target.sh '+target+'; printf "%s" "$PENCHAT_BUILD_CXX"'],cwd=ROOT,text=True)
helper=OUT/'tap';subprocess.run([compiler,'-std=c++11','-Os',str(ROOT/'tests/device/tap.cpp'),'-o',str(helper)],check=True)
push(helper,HELPER);sh('chmod 755 '+HELPER)
event=sh(HELPER+' --discover')
assert event.startswith('/dev/input/event') and event.rsplit('event',1)[1].isdigit()
host=sh('pidof miniapp');boot=sh('cat /proc/sys/kernel/random/boot_id')
if sh('test ! -e '+SAVED+' && test ! -e '+PUBSAVED+' && echo safe')!='safe':raise RuntimeError('Previous isolation needs recovery')
has_data=sh('test -d '+DATA+' && echo yes')=='yes';has_public=sh('test -d '+PUBLIC+' && echo yes')=='yes'
original=[];public_hashes=hashes(PUBLIC) if has_public else []
private=False;public=False
result={'serial':args.serial,'version':APP['version'],'logical_size':[W,H],'host_unchanged':False,'restored':False}
recovery={'serial':args.serial,'host':host,'boot':boot,'data':DATA,'public':PUBLIC,
          'has_data':has_data,'has_public':has_public,'scale':original_scale,'theme':original_theme,
          'public_hashes':public_hashes}
(OUT/'recovery.json').write_text(json.dumps(recovery,ensure_ascii=False,indent=2)+'\n')
try:
    # Shield the original public exports from normal shutdown cleanup.
    if has_public:sh('mv '+PUBLIC+' '+PUBSAVED)
    public=True;sh('mkdir -p '+PUBLIC)
    close()
    original=hashes(DATA) if has_data else []
    recovery['private_hashes']=original
    (OUT/'recovery.json').write_text(json.dumps(recovery,ensure_ascii=False,indent=2)+'\n')
    if has_data:sh('mv '+DATA+' '+SAVED)
    private=True;sh('mkdir -p '+DATA)
    with tempfile.TemporaryDirectory(prefix='penchat-device-layout-') as temp:
        dbpath=pathlib.Path(temp)/'penchat.db';schema=(ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
        with sqlite3.connect(dbpath) as db:
            db.executescript(schema);db.execute("insert into app_settings values('onboarding',?,1)",(json.dumps({'version':1,'complete':True,'step':6}),))
            db.execute("insert into providers(id,name,type,base_url,api_key,created_at,updated_at) values('p','DPI fixture','openai-compatible','http://127.0.0.1:1/v1','test-only-fake-key',1,1)")
            db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,is_default,created_at,updated_at) values('a','p','fixture','DPI fixture','chat',1,1,1)")
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at,default_provider_id,default_model_config_id) values('dpi-chat','DPI acceptance','active',1,1,1,'p','a')")
            db.execute("insert into app_settings values('active_session_id','\"dpi-chat\"',1)")
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('dpi-msg','dpi-chat',1,'assistant','complete','text',?,1,1)",('# 中文标题 English\n\n普通正文与 **中文粗体 Bold**。\n\n- 列表第一项：中文 English\n- 列表第二项：清晰阅读\n\n| 字段 | 内容 |\n|---|---|\n| A | 中文与布局 |\n| B | English 123 |\n\n公式 $x^2+1$ 与中文。\n\n$$\n\\frac{a+b}{2}=c\n$$\n\n'+('阅读和滚动测试。\n\n'*8),))
            db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        push(dbpath,DATA+'/penchat.db')
    push(args.package,REMOTE);installed=sh('miniapp_cli install '+REMOTE)
    assert '"ret": 0' in installed,installed
    sh('miniapp_cli start '+APP['appid']);time.sleep(2)
    if not pid():touch(122,H-47)
    wait(pid);d=wait(foreground_ready);time.sleep(.6);assert d['width']==W and d['height']==H
    capture('chat')
    # Drawers and menus use current diagnostic bounds; no resolution literals.
    d=state();touch(d['screen_safety']['navigation_x']+24 if H>W else 40,d['safe_insets'][1]+24 if H>W else 32)
    wait(lambda:state()['navigation_open']);capture('drawer')
    row=state()['history_rows'][0];touch(row['x'],row['y'],650)
    wait(lambda:state()['history_context']);capture('history-menu')
    touch(W-12,H//2);wait(lambda:not state()['modal'])
    tap_bounds(state()['navigation_footer']['settings']['bounds'])
    wait(lambda:state()['view']=='settings');capture('settings')
    assert len(state()['settings']['rows'])==27
    if args.scale_settings or args.theme_settings:
        result['scale_settings']=[]
        for percent in ((75,125,150,100) if args.scale_settings else (100,)):
            reveal(lambda:state()['settings']['rows']['ui_scale']['bounds'])
            tap_bounds(state()['settings']['rows']['ui_scale']['bounds'])
            wait(lambda:state()['view']=='ui_scale')
            reveal(lambda:state()['ui_scale']['slider'])
            b=state()['ui_scale']['slider'];touch(b[0]+round((percent-75)*(b[2]-b[0])/75),(b[1]+b[3])//2)
            for _ in range(4):
                value=state()['ui_scale']['draft']
                if value==percent:break
                page_action('增大' if value<percent else '减小')
            assert state()['ui_scale']['draft']==percent
            page_action('保存');wait(lambda:state()['view']=='settings' and state()['ui_scale']['applied']==percent)
            capture('scale-'+str(percent))
            # A normal native restart verifies the stored setting; Falcon stays alive.
            close();sh('miniapp_cli start '+APP['appid']);wait(pid)
            d=stable_foreground()
            assert d['ui_scale']['saved']==percent and d['ui_scale']['applied']==percent
            tap_bounds(d['composer_text_bounds'])
            wait(lambda:state()['editor']['active'] and state()['editor']['keys'])
            e=state()['editor'];assert e['requested_scale']==percent and min(100,percent)<=e['effective_scale']<=percent
            space=e['coordinate_space']
            for k in e['keys']:
                a,b,c,d=k['bounds'];assert 0<=a<c<space['width'] and 0<=b<d<space['height']
            capture('scale-keyboard-'+str(percent));action('done');wait(lambda:not state()['editor']['active'])
            d=state();dp=lambda n:(n*percent+50)//100
            x=d['screen_safety']['navigation_x'] if H>W else dp(16)
            y=d['safe_insets'][1] if H>W else dp(8)
            touch(x+max(44,dp(48))//2,y+max(44,dp(48))//2);wait(lambda:state()['navigation_open'])
            tap_bounds(state()['navigation_footer']['settings']['bounds']);wait(lambda:state()['view']=='settings')
            result['scale_settings'].append({'percent':percent,'restart':True,'keyboard_effective':e['effective_scale']})
            if args.theme_settings:
                for mode in ('light','dark'):
                    apply_theme(mode)
                    capture('theme-'+mode+'-scale-'+str(percent)+'-settings')
                    close();sh('miniapp_cli start '+APP['appid']);wait(pid)
                    d=stable_foreground()
                    assert d['appearance']['theme']==mode and not d['appearance']['pending']
                    assert d['ui_scale']['saved']==percent and d['ui_scale']['applied']==percent
                    capture_markdown('theme-'+mode+'-scale-'+str(percent))
                    open_settings()
                    result.setdefault('theme_settings',[]).append({'percent':percent,'theme':mode,'restart':True})
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
    # Finish through the live native field action. Opening system IME and then
    # starting this app again does not cancel that editor; close_app would have
    # to terminate native while Falcon still owns it and leave a failure page.
    # Observed system controls/confirmation belong to the dedicated isolated
    # keyboard-handoff and official-editing release scopes.
    action('cancel');wait(lambda:not state()['editor']['active'])
    result['native_field_cancelled']=True
    result['official_keyboard_scope']='Separate keyboard-handoff / official-editing acceptance'
    result['layout']='compact' if s6 else 'portrait' if H>W else 'wide'
    result['typed_letters']=4
    result['settings_rows']=27
    result['history_menu']=True
    result['provider_field']=True
except BaseException as error:
    result['error']=str(error)
    try:(OUT/'failure-diagnostic.json').write_text(json.dumps(state(),ensure_ascii=False,indent=2)+'\n')
    except Exception:pass
    raise
finally:
    with defer_cleanup_signals():
        recovery_errors=[]
        try:close()
        except Exception as error:recovery_errors.append('native close: '+str(error))
        # Never replace a data directory while native code could still write to it.
        try:stopped=not pid()
        except Exception as error:
            stopped=False;recovery_errors.append('native status: '+str(error))
        for isolated,current,saved,existed in ((private,DATA,SAVED,has_data),(public,PUBLIC,PUBSAVED,has_public)):
            if not isolated:continue
            if not stopped:
                recovery_errors.append('retained isolation for recovery: '+saved);continue
            try:
                sh('rm -rf '+current)
                if existed:sh('mv '+saved+' '+current)
            except Exception as error:recovery_errors.append('restore '+current+': '+str(error))
        try:
            result['restored']=(not private or not has_data or hashes(DATA)==original) and (not public or not has_public or hashes(PUBLIC)==public_hashes) and not recovery_errors
            result['host_unchanged']=sh('pidof miniapp')==host and sh('cat /proc/sys/kernel/random/boot_id')==boot
        except Exception as error:recovery_errors.append('restore verification: '+str(error))
        try:sh('rm -f '+REMOTE)
        except Exception as error:recovery_errors.append('helper cleanup: '+str(error))
        if recovery_errors:result['recovery_errors']=recovery_errors
        (OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        assert result['restored'] and result['host_unchanged'] and not recovery_errors,result
        sh('miniapp_cli start '+APP['appid'])
# Verify the executable actually running after restoring the original data.
wait(pid)
d=state()
with zipfile.ZipFile(args.package) as package:
    expected=hashlib.sha256(package.read('assets/bin/penchat-lvgl')).hexdigest()
actual=sh('sha256sum /proc/'+pid()+'/exe').split()[0]
assert actual==expected, 'Live executable does not match the installed payload'
if original_scale is None:original_scale=d.get('ui_scale',{}).get('saved',100)
if original_theme is None:original_theme=d.get('appearance',{}).get('theme','dark')
assert d['appearance']['theme']==original_theme and not d['appearance']['pending']
assert d['editor']['requested_scale']==original_scale and min(100,original_scale)<=d['editor']['effective_scale']<=original_scale
result.update(native_ready_after_restore=True,native_sha256=actual,restored_scale=original_scale,restored_theme=original_theme)
sh('rm -f '+HELPER)
(OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False))
