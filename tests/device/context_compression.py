#!/usr/bin/env python3
"""ORANGE compression acceptance using full directory isolation and fake local HTTP.
Never restarts the MiniApp host or reads real provider credentials into diagnostics.
"""
import hashlib, json, os, pathlib, shlex, sqlite3, subprocess, sys, tempfile, time
import acceptance as d
from chat_acceptance import close_app, pull, push
sys.path.insert(0, str(d.ROOT/'tests'))
from test_context import fixture
OUT=d.ROOT/'.work/device-context';OUT.mkdir(parents=True,exist_ok=True)
DATA=d.DATA;SAVED=DATA+'.context-acceptance-saved'
PUBLIC='/userdisk/Favorite/PenChat';PUBLIC_SAVED=PUBLIC+'.context-acceptance-saved'
RESULTS=[]
PHOTO=str(pathlib.PurePosixPath(DATA).parent)+'//data/assets/asset_1_1.png'
def shell(command): return d.adb(command)
def hashes(path):
    return sorted(shell('find '+shlex.quote(path)+' -type f -exec sha256sum {} \\;').splitlines())
def capture(name):
    time.sleep(.15)
    if d.state()['keyboard']:
        shell('miniapp_cli capture /tmp/penchat-context-screen.png');pull('/tmp/penchat-context-screen.png',OUT/(name+'.png'))
    else:
        d.state(screenshot=True);pull(DATA+'/lvgl-diagnostic.png',OUT/(name+'.png'))
def swipe(x,y1,y2):
    shell('/tmp/penchat-ui-tap '+str(y1+113)+' '+str(959-x)+' '+str(y2+113)+' '+str(959-x)+' 25 2>/dev/null');time.sleep(.2)
def logs():
    return [json.loads(line) for line in shell('cat /tmp/penchat-http-requests.jsonl 2>/dev/null || true').splitlines() if line]
def start():
    shell('miniapp_cli start '+d.APP_ID);d.wait(d.pid,timeout=20)
    def ready():
        try:
            s=d.state()
            return s if s['view']=='chat' and s['visible'] else None
        except AssertionError:return None
    s=d.wait(ready,timeout=40)
    if s['to_bottom_visible']:d.tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
    d.wait(lambda:d.state()['toolbar_y']==208,timeout=15)
def mode(value):shell('echo '+shlex.quote(value)+' > /tmp/penchat-http-mode')
def idle():d.wait(lambda:not d.state().get('compressing',False) and not d.state()['generating'],timeout=45)
def snapshot():
    pull(DATA+'/penchat.db',OUT/'current.db')
    return sqlite3.connect(OUT/'current.db')
def checkpoint():
    with snapshot() as db:
        row=db.execute("select metadata_json from chat_sessions where id='s'").fetchone()
        return json.loads(row[0]).get('context_compression',{}) if row else {}
def type_draft(text):
    d.to_chat();d.wait(lambda:d.state()['toolbar_y']==208)
    d.tap(500,232);d.wait(lambda:len(d.state()['editor'].get('keys',[]))>=26,timeout=20)
    if d.state()['editor']['chinese']:d.tap(149,238)
    for char in text:
        key=next(k for k in d.state()['editor']['keys'] if k['key']==char)
        d.tap(key['x'],key['y'])
    d.tap(880,236);d.wait(lambda:not d.state()['editor']['active'])
    assert d.state()['draft_length']==len(text)
def manual():
    d.to_chat();d.attachment_action("compress",swipe)
def seed(api='responses',following=False,rounds=40,budget=16384):
    close_app()
    # DATA is a disposable directory; originals are moved intact to SAVED.
    shell('rm -f '+DATA+'/penchat.db '+DATA+'/penchat.db-journal '+DATA+'/penchat.db.backup-v1')
    shell('rm -rf '+DATA+'/assets; mkdir '+DATA+'/assets')
    with tempfile.TemporaryDirectory(prefix='penchat-device-context-') as tmp:
        p=pathlib.Path(tmp);fixture(p,18773,rounds=rounds,budget=budget,api=api,image=rounds>0)
        path=PHOTO
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute('update message_assets set original_path=?,stored_path=?',(path,path))
            prefs={'model_id':'a','system_prompt':'CHAT_SYSTEM_ONCE','has_sent':bool(rounds),'effort':'high','compression_model_id':'' if following else 'b'}
            db.execute("update chat_sessions set title='设备压缩测试',metadata_json=? where id='s'",(json.dumps({'title_state':'manual','chat_preferences':prefs}),))
            presets=[{'id':'default','name':'默认预设','prompt_mode':'inherit','compression_model_id':''},
                     {'id':'explicit','name':'指定压缩模型','prompt_mode':'inherit','compression_model_id':'b'}]
            db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',('chat_presets',json.dumps(presets)))
            db.execute('insert into app_settings(key,value_json,updated_at) values(?,?,1)',('presets_enabled','true'))
            n=rounds*2+1
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values('pending','s',?,'user','complete','text',1,1,'DEVICE_PENDING')",(n,))
        if rounds:push(p/'assets/fixture.png',path)
        push(p/'penchat.db',DATA+'/penchat.db')
    start()
    shell('rm -f /tmp/penchat-http-requests.jsonl')
    mode('ok')
def delete_history(sid):
    d.open_navigation()
    row=next(r for r in d.state()['history_rows'] if r['id']==sid)
    shell('/tmp/penchat-ui-tap --hold '+str(row['y']+113)+' '+str(959-row['x'])+' 650 2>/dev/null')
    d.wait(lambda:d.state()['history_context']==sid)
    d.history_action("delete")
def compile_helpers():
    sdk=os.environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK')
    tool=os.environ.get('JSAPI_TOOLCHAIN_ROOT',sdk+'/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu')+'/bin/aarch64-linux-gnu-g++'
    for name,source in [('penchat-ui-tap','tap.cpp'),('penchat-key','key.cpp'),('penchat-context-http','http_fixture.cpp')]:
        target=OUT/name
        subprocess.run([tool,'-std=c++14','-Os','-static-libstdc++','-static-libgcc','-I'+str(d.ROOT/'.deps/json/single_include'),str(d.ROOT/'tests/device'/source),'-pthread','-o',str(target)],check=True)
        push(target,'/tmp/'+name);shell('chmod 755 /tmp/'+name)
    push(d.ROOT/'build/orange/penchat-db-probe','/tmp/penchat-db-probe');shell('chmod 755 /tmp/penchat-db-probe')
def run():
    assert 'ORANGE' in shell('cat /proc/device-tree/model') and shell('uname -m')=='aarch64'
    if d.pid():
        current=d.state()
        assert not current['keyboard'] and not current['editor']['active'] and not current['generating'] and not current.get('compressing',False) and current.get('storage',{}).get('unsaved',0)==0 and current['draft_length']==0, 'Finish existing input and save pending content before testing'
        close_app()
    host=shell('pidof miniapp');original=hashes(DATA)
    original_public=hashes(PUBLIC) if shell('test -d '+PUBLIC+' && echo yes')=='yes' else None
    assert shell('test ! -e '+SAVED+' && test ! -e '+PUBLIC_SAVED+' && echo ready')=='ready', 'Prior acceptance backup requires recovery first'
    isolated=False;public=False;child=None;fixture_pid='';restored=False
    compile_helpers()
    try:
        shell('mv '+DATA+' '+SAVED);isolated=True
        if original_public is not None:shell('mv '+PUBLIC+' '+PUBLIC_SAVED);public=True
        shell('mkdir '+DATA)
        package=d.ROOT/'dist'/(d.APP_ID+'.'+d.APP['version'].replace('.','_')+'.amr')
        push(package,'/tmp/penchat-context.amr')
        installed=shell('miniapp_cli install /tmp/penchat-context.amr')
        manifests=shell('find '+str(pathlib.PurePosixPath(DATA).parent)+' -maxdepth 2 -name manifest.json').splitlines()
        assert any(json.loads(shell('cat '+shlex.quote(path)))['version']==d.APP['version'] for path in manifests),installed
        reply=OUT/'reply.md';reply.write_text('设备正文回复完成。\n');push(reply,'/tmp/penchat-http-reply.md')
        assert not shell('pidof penchat-context-http || true')
        child=subprocess.Popen(['adb','-s',d.SERIAL,'shell','exec /tmp/penchat-context-http'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        fixture_pid=d.wait(lambda:shell('pidof penchat-context-http || true'))
        for api in ['responses','chat']:
            seed(api);type_draft('ok');mode('context-delay');d.tap(908,232)
            d.wait(lambda:d.state()['compressing'],timeout=10);d.wait(lambda:d.state()['scan_released'],timeout=10);capture('progress-'+api)
            idle();assert not d.state()['modal']
            cp=checkpoint();assert cp['through']==72 and cp['revision']==1,cp
            calls=logs();compressed=[c for c in calls if c['compression']];answer=calls[-1]
            assert len(compressed)>1 and all(c['model']=='b' and c['effort']=='' and c['system_count']==1 and c['reasoning_items']==0 for c in compressed),compressed
            assert sum(c['images'] for c in compressed)==1 and any(c['has_old0'] for c in compressed)
            assert not answer['compression'] and answer['model']=='a' and answer['has_reference'] and answer['has_pending'] and not answer['has_old0'] and answer['images']==0 and answer['effort']=='high' and answer['system_count']==1,answer
            with snapshot() as db:
                assert db.execute("select count(*) from chat_sessions where status='archived'").fetchone()[0]==1
                assert db.execute('select count(*) from chat_messages where session_id=?',(cp['archive_id'],)).fetchone()[0]==81
                assert db.execute('select count(distinct stored_path),count(*) from message_assets').fetchone()==(1,2)
                assert not db.execute('pragma foreign_key_check').fetchall()
            d.tap(40,168);capture('summary-'+api);d.tap(150,222);d.view('archives');capture('archive-'+api)
            d.tap(726,76);assert d.state()['session']=='s';d.to_chat()
            type_draft('keep');mode('ok');manual();idle();assert checkpoint()['revision']==2
            assert d.state()['draft_length']==4;capture('manual-summary-'+api);d.tap(890,222)
            with snapshot() as db:assert db.execute('select count(*) from chat_sessions').fetchone()[0]==3
            RESULTS.append(api+'-auto-manual-image-archive-restore-and-frozen-request')
            # New chat inherits only the selection, never coverage; old copies remain independent.
            d.open_navigation();d.tap(180,236);assert not d.state()['session_preferences'].get('context_compression')
            d.open_settings();d.setting("archives");d.view('archives');capture('archives-after-restore-'+api)
            d.tap(846,76)
            with snapshot() as db:
                assert db.execute('select count(*) from chat_sessions').fetchone()[0]==2
                assert db.execute('select count(*) from message_assets').fetchone()[0]==2
            assert shell('test -f '+PHOTO+' && echo yes')=='yes'
            RESULTS.append(api+'-archive-delete-keeps-shared-image')
            delete_history('s');assert shell('test -f '+PHOTO+' && echo yes')=='yes'
            delete_history(cp['archive_id'])
            d.wait(lambda:shell('test ! -e '+PHOTO+' && echo gone')=='gone',timeout=15)
            RESULTS.append(api+'-last-shared-reference-collected')
        seed();type_draft('keep');mode('context-delay');d.tap(908,232)
        d.wait(lambda:d.state()['compressing']);d.tap(890,222)
        d.open_settings();d.setting("models");d.tap(300,175)
        assert d.state()['active_model']=='b'
        idle();assert d.state()['active_model']=='b' and logs()[-1]['model']=='a'
        d.to_chat();type_draft('next');mode('ok');d.tap(908,232);idle()
        assert logs()[-1]['model']=='b'
        RESULTS.append('model-switch-during-compression-only-affects-next-request')
        # Failure/cancel paths preserve drafts, photos and original checkpoint.
        for case in ['http-error','stop','switch','home','background']:
            seed();type_draft('keep');mode('error' if case=='http-error' else 'context-delay')
            d.tap(908,232)
            if case=='http-error':
                idle();assert d.state()['modal'];capture('http-error');d.tap(890,222)
            else:
                d.wait(lambda:d.state()['compressing'])
                if case=='stop':d.tap(150,222);idle()
                elif case=='switch':d.tap(890,222);d.open_navigation();d.tap(180,236);idle()
                elif case=='home':d.home();start();idle()
                else:
                    native=d.pid();shell('miniapp_cli start 8080222437664451');d.wait(lambda:not d.state()['visible'])
                    time.sleep(3);shell('miniapp_cli start '+d.APP_ID);d.wait(lambda:d.state()['visible']);idle()
                    assert d.pid()==native
            if case=='background':
                # Background alone may finish atomically; it must send only once and resume intact.
                assert checkpoint().get('revision',0) in [0,1]
                RESULTS.append('background-completion-and-resume');continue
            assert not checkpoint(),case
            with snapshot() as db:assert db.execute('select count(*) from chat_sessions').fetchone()[0]==1,case
            if case in ['http-error','stop']:assert d.state()['draft_length']==4,case
            assert all(c['compression'] for c in logs()),case
            RESULTS.append(case+'-no-archive-or-late-send')
        seed(rounds=1,budget=16384,following=True);type_draft('keep');manual();idle();assert checkpoint()['model_id']=='a'
        assert d.state()['draft_length']==4;d.tap(890,222);RESULTS.append('short-manual-follows-chat-model')
        d.open_settings();swipe(350,230,60);capture('settings')
        # Native model and preset forms, plus real system IME handoff.
        d.to_chat();d.open_settings();d.setting("models");d.tap(870,175);d.view('model');swipe(450,230,60);capture('model-budget');d.page_action("保存")
        d.to_chat();d.open_settings();d.setting("presets");d.view('presets');d.tap(300,166);d.view('preset_edit');capture('preset-compression-model')
        # Save a concrete compression model, then explicitly restore following.
        d.tap(300,192);d.wait(lambda:d.state()['modal']);capture('compression-model-options')
        d.tap(300,186);d.page_action("保存");d.view('presets')
        d.tap(300,166);d.tap(300,192);d.tap(300,64);d.page_action("保存");d.view('presets')
        d.to_chat()
        for y,expected in [(138,'b'),(66,'')]:
            d.attachment_action('presets',swipe);d.wait(lambda:d.state()['popup']=='presets')
            d.tap(350,y);assert d.state()['session_preferences']['compression_model_id']==expected
            assert d.state()['active_model']=='a' and d.state()['draft_length']==4
        RESULTS.append('preset-explicit-model-and-clear-to-follow')
        d.to_chat();d.tap(500,232);d.official_editor();capture('official-ime-native-paused')
        assert 'allocated by = penchat-lvgl' not in shell('cat /sys/kernel/debug/dri/0/state')
        d.tap(35,35);d.cancel_native_editor();assert d.state()['draft_length']==4
        RESULTS.append('settings-budget-preset-and-official-ime-cancel')
        # Check repeated child lifecycle without modifying/restarting the system host.
        for i in range(20):
            d.home();assert 'allocated by = penchat-lvgl' not in shell('cat /sys/kernel/debug/dri/0/state')
            start();assert shell('pidof miniapp')==host
        RESULTS.append('20-home-reopen-cycles-drm-cleanup')
        assert shell('pidof miniapp')==host
    except BaseException:
        if d.pid():
            capture('failure')
            s=d.state();print(json.dumps({k:s.get(k) for k in ['view','compressing','generating','modal','draft_length','popup','scroll_y']},ensure_ascii=False),flush=True)
        raise
    finally:
        if isolated:
            close_app()
            if fixture_pid:shell('kill -TERM '+fixture_pid+' 2>/dev/null || true')
            if child:child.wait(timeout=10)
            shell('rm -rf '+DATA+'; mv '+SAVED+' '+DATA)
            if public:shell('rm -rf '+PUBLIC+'; mv '+PUBLIC_SAVED+' '+PUBLIC)
            elif original_public is None:shell('rm -rf '+PUBLIC)
            assert hashes(DATA)==original,'Original private files did not restore exactly'
            if original_public is not None:assert hashes(PUBLIC)==original_public,'Public import/export files changed'
            restored=True
            shell('rm -f /tmp/penchat-context.amr /tmp/penchat-context-http /tmp/penchat-http-mode /tmp/penchat-http-reply.md /tmp/penchat-http-requests.jsonl /tmp/penchat-title-requests.jsonl /tmp/penchat-db-snapshot.db')
            assert shell('pidof miniapp')==host
            (OUT/'results.json').write_text(json.dumps({'version':d.APP['version'],'passed':RESULTS,'original_files_restored_exactly':restored,'host_unchanged':shell('pidof miniapp')==host},ensure_ascii=False,indent=2)+'\n')
            start()
        (OUT/'results.json').write_text(json.dumps({'version':d.APP['version'],'passed':RESULTS,'original_files_restored_exactly':restored,'host_unchanged':shell('pidof miniapp')==host},ensure_ascii=False,indent=2)+'\n')
    print('ORANGE context acceptance: '+', '.join(RESULTS)+'; original private/public files restored exactly; host unchanged.')
if __name__=='__main__':run()
