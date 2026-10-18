#!/usr/bin/env python3
"""Use an isolated fake provider, then restore the exact device DB snapshot."""
import json, pathlib, shutil, sqlite3, subprocess, sys, time, shlex
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from test_lvgl_app import REPLY
import acceptance as device
ROOT=device.ROOT;OUT=ROOT/'.work/device-chat';OUT.mkdir(parents=True,exist_ok=True)
SERIAL=device.SERIAL;DATA=device.DATA
def mode(value):device.adb("echo "+value+" > /tmp/penchat-http-mode")
def requests():return [json.loads(line) for line in device.adb('cat /tmp/penchat-http-requests.jsonl 2>/dev/null || true').splitlines() if line]

def pull(remote,local):subprocess.run(['adb','-s',SERIAL,'pull',remote,str(local)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def push(local,remote):subprocess.run(['adb','-s',SERIAL,'push',str(local),remote],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def close_app():
    device.adb('miniapp_cli start 8080222437664451');time.sleep(.15)
    p=device.pid()
    if p:
        assert device.APP_ID in device.adb('tr \'\\0\' \' \' < /proc/'+p+'/cmdline')
        device.adb('kill -TERM '+p)
    device.wait(lambda:not device.pid())
    time.sleep(.1)
def start():device.adb('miniapp_cli start '+device.APP_ID);device.wait(device.pid);time.sleep(.25);device.view('chat')
def send_request(send=True,api=None):
    # Seed an unsent fake user message while native is stopped; an empty draft
    # with no pending user content is deliberately disabled by the new composer.
    close_app();pull(DATA+'/penchat.db',OUT/'queued.db')
    with sqlite3.connect(OUT/'queued.db') as db:
        now=int(time.time()*1000)
        seq=db.execute("select coalesce(max(seq),0)+1 from chat_messages where session_id='lvgl_acceptance_session'").fetchone()[0]
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?, 'lvgl_acceptance_session',?,'user','complete','text','fixture request',?,?)",('fixture_user_'+str(now),seq,now,now))
        db.execute("update app_settings set value_json='\"lvgl_acceptance_session\"' where key='active_session_id'")
        if api:db.execute("update providers set metadata_json=? where id='lvgl_acceptance_provider'",(json.dumps({'api':api,'default_effort':'medium'}),))
        db.commit()
    push(OUT/'queued.db',DATA+'/penchat.db');start()
    if send:device.tap(908,232)
def messages():
    pull(DATA+'/penchat.db',OUT/'current.db')
    with sqlite3.connect(OUT/'current.db') as db:return db.execute("select status,output_text,error_json from chat_messages where role='assistant' and session_id='lvgl_acceptance_session' order by seq").fetchall()
def session_count():
    pull(DATA+'/penchat.db',OUT/'session-count.db')
    with sqlite3.connect(OUT/'session-count.db') as db:
        return db.execute('select count(*) from chat_sessions').fetchone()[0]
def session_info(id='lvgl_acceptance_session'):
    pull(DATA+'/penchat.db',OUT/'session-info.db')
    with sqlite3.connect(OUT/'session-info.db') as db:
        rows=db.execute('select title,status,metadata_json from chat_sessions where id=?',(id,)).fetchall()
    return (rows[0][0],rows[0][1],json.loads(rows[0][2])) if rows else None
def history_menu():
    device.open_navigation()
    row=next(r for r in device.state()['history_rows'] if r['id']=='lvgl_acceptance_session')
    device.adb('/tmp/penchat-ui-tap --hold '+str(row['y']+113)+' '+str(959-row['x'])+' 650 2>/dev/null')
    assert device.state()['history_context']=='lvgl_acceptance_session'
def wait_reply(count,status):
    device.wait(lambda:len(messages())>=count and messages()[-1][0]==status,timeout=15)
def seed(path,api):
    db=sqlite3.connect(path);now=int(time.time()*1000)
    db.execute("insert or replace into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('lvgl_acceptance_session','LVGL acceptance','active',?,?,?)",(now,now,now))
    db.execute("insert or replace into providers(id,name,type,base_url,api_key,headers_json,metadata_json,created_at,updated_at,is_default) values('lvgl_acceptance_provider','LVGL fixture','openai-compatible','http://127.0.0.1:18773/v1','test-only-no-real-key','{}',?,?,?,1)",(json.dumps({'api':api,'default_effort':'medium'}),now,now))
    db.execute("update providers set enabled=0 where id!='lvgl_acceptance_provider'")
    db.execute('update model_configs set is_default=0')
    db.execute("insert or replace into model_configs(id,provider_id,name,display_name,model_type,created_at,updated_at,is_default) values('lvgl_acceptance_model','lvgl_acceptance_provider','fixture','fixture','chat',?,?,1)",(now,now))
    db.execute("update model_configs set capabilities_json=? where id='lvgl_acceptance_model'",(json.dumps({'reasoning':{'levels':{'medium':'medium','high':'high'},'summary':True}}),))
    db.execute("insert or replace into model_configs(id,provider_id,name,display_name,model_type,capabilities_json,created_at,updated_at) values('lvgl_acceptance_model2','lvgl_acceptance_provider','fixture-alt','fixture-alt','chat',?,?,?)",(json.dumps({'reasoning':{'levels':{'high':'high','max':'max'},'summary':True}}),now,now))
    db.execute("insert or replace into app_settings(key,value_json,updated_at) values('active_session_id','\"lvgl_acceptance_session\"',?)",(now,))
    db.execute("insert or replace into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values('lvgl_acceptance_prompt','lvgl_acceptance_session',1,'user','complete','text',?,?,?)",(now,now,'你好，验证 Markdown 和数学公式。'))
    db.execute("insert or replace into app_settings(key,value_json,updated_at) values('penchat_settings',?,?)",(json.dumps({'autoSendCapturedContent':False}),now))
    db.execute("insert or replace into app_settings(key,value_json,updated_at) values('auto_title_enabled','true',1)")
    db.execute("insert or replace into app_settings(key,value_json,updated_at) values('title_model_id','\"\"',1)")
    db.execute("insert or replace into app_settings(key,value_json,updated_at) values('reasoning_display','\"collapsed\"',1)")
    db.commit();db.close()

if __name__=='__main__':
    assert 'ORANGE' in device.adb('cat /proc/device-tree/model')
    close_app();pull(DATA+'/penchat.db',OUT/'before.db')
    before_files=set(device.adb('find '+DATA+'/DCIM '+DATA+'/assets -type f 2>/dev/null || true').splitlines())
    parent=device.adb('pidof miniapp')
    sdk=__import__('os').environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK')
    toolchain=__import__('os').environ.get('JSAPI_TOOLCHAIN_ROOT',sdk+'/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu')
    fixture=OUT/'http-fixture'
    subprocess.run([toolchain+'/bin/aarch64-linux-gnu-g++','-std=c++14','-Os','-static-libstdc++','-static-libgcc','-I'+str(ROOT/'.deps/json/single_include'),str(ROOT/'tests/device/http_fixture.cpp'),'-pthread','-o',str(fixture)],check=True)
    push(fixture,'/tmp/penchat-http-fixture');device.adb('chmod 755 /tmp/penchat-http-fixture')
    tap_fixture=OUT/'tap-fixture'
    subprocess.run([toolchain+'/bin/aarch64-linux-gnu-g++','-std=c++11','-Os',str(ROOT/'tests/device/tap.cpp'),'-o',str(tap_fixture)],check=True)
    push(tap_fixture,'/tmp/penchat-ui-tap');device.adb('chmod 755 /tmp/penchat-ui-tap')
    (OUT/'reply.md').write_text(REPLY);push(OUT/'reply.md','/tmp/penchat-http-reply.md')
    device.adb('rm -f /tmp/penchat-http-requests.jsonl /tmp/penchat-title-requests.jsonl');mode('ok')
    old=device.adb('pidof penchat-http-fixture || true')
    if old:device.adb('kill -TERM '+old)
    fixture_process=subprocess.Popen(['adb','-s',SERIAL,'shell','exec /tmp/penchat-http-fixture'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    fixture_pid=device.wait(lambda:device.adb('pidof penchat-http-fixture || true'))
    assert fixture_pid.isdigit(),'Fixture did not stay running'
    results=[];gesture_processes=[]
    try:
        shutil.copy2(OUT/'before.db',OUT/'fixture.db');seed(OUT/'fixture.db','responses');push(OUT/'fixture.db',DATA+'/penchat.db');start()
        device.tap(908,232);wait_reply(1,'complete');assert messages()[-1][1]==REPLY;device.capture('stream-responses');results.append('responses')
        device.wait(lambda:session_info()[2].get('title_state')=='complete')
        assert len(session_info()[0])<=device.state()['title_limit'];results.append('first-answer-auto-title')
        mode('error');send_request();wait_reply(2,'error');assert '401' in messages()[-1][2];results.append('http401')
        mode('cut');send_request();wait_reply(3,'error');assert messages()[-1][1]==REPLY;results.append('cut')
        mode('cancel');send_request();time.sleep(.4);device.tap(908,232);wait_reply(4,'cancelled');results.append('cancel')
        mode('ok')
        # Enable image auto-send using the actual settings UI, then take a real photo.
        device.open_settings();device.tap(300,134);device.to_chat()
        device.tap(128,232);device.tap(200,38);device.view('camera');time.sleep(.5);device.tap(888,133);device.view('chat');wait_reply(5,'complete');assert requests()[-1]['has_image'];results.append('camera-auto-send')
        # Disable auto-send, pick the saved photo through the real album UI, then send manually.
        device.open_settings();device.tap(300,134);device.to_chat()
        count=len(requests());device.tap(128,232);device.tap(200,82);device.view('album');device.tap(200,95);device.view('chat');time.sleep(.2)
        assert len(requests())==count and len(messages())==5
        # Long press removes only the pending attachment after confirmation.
        photos=device.state()['chat_images'];photo=max(photos,key=lambda p:p['y'])
        def hold_photo():
            x=photo['x']+photo['width']//2;y=photo['y']+photo['height']//2
            device.adb('/tmp/penchat-ui-tap --hold '+str(y+113)+' '+str(959-x)+' 650 2>/dev/null')
            device.wait(lambda:device.state()['modal'])
        hold_photo();device.capture('pending-image-delete');device.tap(890,225)
        assert len(device.state()['chat_images'])==len(photos)
        hold_photo();device.tap(770,225)
        device.wait(lambda:len(device.state()['chat_images'])==len(photos)-1)
        assert len(requests())==count
        device.tap(128,232);device.tap(200,82);device.view('album');device.tap(200,95);device.view('chat')
        results.append('pending-image-confirmed-delete')
        device.tap(908,232);wait_reply(6,'complete');assert requests()[-1]['has_image'];results.append('album-manual-send')
        close_app();pull(DATA+'/penchat.db',OUT/'fixture-chat.db')
        with sqlite3.connect(OUT/'fixture-chat.db') as db:
            db.execute("update providers set metadata_json=? where id='lvgl_acceptance_provider'",(json.dumps({'api':'chat'}),));db.commit()
        push(OUT/'fixture-chat.db',DATA+'/penchat.db');start();send_request();wait_reply(7,'complete');assert requests()[-1]['path'].endswith('/chat/completions');results.append('chat-completions')
        saved_sessions=session_count()
        initial=device.state();original=initial['session'];stable_bottom=initial['scroll_y']
        device.adb('/tmp/penchat-ui-tap 303 459 258 459 2>/dev/null');time.sleep(.2)
        assert device.state()['session']==original
        device.adb('/tmp/penchat-ui-tap 303 459 175 459 2>/dev/null');time.sleep(.2)
        boundary=device.state()
        assert boundary['session']==original and boundary['pull_full_distance']==129
        assert boundary['pull_reader_height']==208
        results.append('128px-pull-does-not-create')
        # Returning from older messages requires a quiet 500ms; repeated
        # urgent swipes reset it rather than triggering a new conversation.
        device.adb('/tmp/penchat-ui-tap 173 459 343 459 2>/dev/null');time.sleep(.45)
        assert not device.state()['following_latest']
        assert device.state()['pull_pause_ms']==500
        # Jump and subsequent swipes share one device shell. Sampling the jump
        # on the host can itself consume the entire 500ms quiet interval.
        # Keep the swipes continuous on-device; host diagnostics must not insert
        # an idle gap longer than the guard between individual gestures.
        rapid=subprocess.Popen(['adb','-s',SERIAL,'shell',
            '/tmp/penchat-ui-tap 287 438 2>/dev/null && '
            'for attempt in 1 2 3; do /tmp/penchat-ui-tap 303 459 151 459 2>/dev/null || exit 1; done'],stdout=subprocess.DEVNULL)
        gesture_processes.append(rapid)
        guarded_samples=[]
        while rapid.poll() is None:
            current=device.state()
            if not current['following_latest']:continue
            guarded_samples.append(current)
            assert current['session']==original and not current['pull_create_allowed'], {k:current[k] for k in ['session','pull_active','pull_create_allowed','pull_progress','pull_guard_remaining_ms']}
            assert current['scroll_y']==stable_bottom and current['scroll_bottom']==0
            assert -110<=current['pull_content_y']<=0
            time.sleep(.02)
        assert rapid.wait()==0 and len(guarded_samples)>=2
        assert any(sample['pull_content_y']<0 for sample in guarded_samples)
        results.append('rapid-pull-guard-controlled-stretch')
        # Rearm the guard, then run fast -> slow partial -> fast full entirely on
        # the device. No host diagnostic delay can supply a quiet interval.
        device.adb('/tmp/penchat-ui-tap 173 459 343 459 2>/dev/null');time.sleep(.45)
        assert not device.state()['following_latest']
        swipe=subprocess.Popen(['adb','-s',SERIAL,'shell',
            '/tmp/penchat-ui-tap 287 438 2>/dev/null && '
            '/tmp/penchat-ui-tap 303 459 151 459 2>/dev/null && '
            '/tmp/penchat-ui-tap 303 459 233 459 80 2>/dev/null && '
            '/tmp/penchat-ui-tap 303 459 174 459 2>/dev/null'],stdout=subprocess.DEVNULL)
        gesture_processes.append(swipe)
        samples=[]
        while swipe.poll() is None:
            current=device.state()
            if current['pull_active']:
                samples.append(current)
                assert current['scroll_y']==stable_bottom
                assert current['send_center_x2']==current['capsule_endcap_x2']
                assert current['send_center_y2']==current['capsule_center_y2']
                if current['pull_create_allowed']:assert current['pull_guard_remaining_ms']==0
            time.sleep(.03)
        assert swipe.wait()==0 and len(samples)>=2
        (OUT/'pull-samples.json').write_text(json.dumps([{k:s[k] for k in ['pull_active','pull_create_allowed','pull_progress','pull_guard_remaining_ms','pull_content_y','session','touch']} for s in samples],indent=2)+'\n')
        assert any(s['pull_create_allowed'] and 0<s['pull_progress']<100 for s in samples)
        results.append('slow-pull-bypass-and-progress-release-no-wait')
        results.append('stable-pull-and-concentric-send')
        time.sleep(.25)
        assert device.state()['session']!=original and session_count()==saved_sessions
        device.capture('pull-created-chat');results.append('pull-new-chat-129px')
        fresh_session=device.state()['session']
        assert device.state()['pull_guard_remaining_ms']==0
        device.adb('/tmp/penchat-ui-tap 303 459 151 459 2>/dev/null')
        assert device.state()['session']==fresh_session and session_count()==saved_sessions
        results.append('empty-chat-pull-blocked')
        gesture_session=device.state()['session']
        device.open_navigation();device.tap(180,236);device.view('chat')
        assert device.state()['session']!=gesture_session and session_count()==saved_sessions;results.append('history-new-chat')
        # Backdrop movement must not be interpreted as a click.
        device.open_navigation()
        device.adb('/tmp/penchat-ui-tap 303 159 151 159 2>/dev/null')
        assert device.state()['navigation_open']
        device.adb('/tmp/penchat-ui-tap 213 159 213 59 2>/dev/null')
        assert device.state()['navigation_open']
        device.tap(800,100);assert not device.state()['navigation_open'];results.append('drawer-tap-only')
        # Select a model and explicit effort entirely through native controls.
        send_request(False,'responses')
        device.tap(128,232);device.tap(220,126);assert device.state()['popup']=='models'
        device.capture('model-picker');device.tap(300,114)
        assert device.state()['active_model']=='lvgl_acceptance_model2'
        device.tap(128,232);device.tap(220,170);device.tap(858,232)
        assert device.state()['popup']=='effort'
        device.tap(896,110);assert device.state()['effective_effort']=='max'
        device.capture('effort-high-max');device.tap(800,230)
        mode('reasoning');device.tap(908,232);wait_reply(8,'complete')
        assert requests()[-1]['model']=='fixture-alt' and requests()[-1]['effort']=='max'
        state=device.state();assert state['reasoning_header_count']==1
        device.tap(state['reasoning_header_x'],state['reasoning_header_y'])
        assert device.state()['reasoning_expanded_count']==1
        device.capture('reasoning-expanded');results.append('model-effort-summary')
        send_request();wait_reply(9,'complete');assert requests()[-1]['reasoning_items']>0
        results.append('encrypted-reasoning-replay')
        mode('encrypted');send_request();wait_reply(10,'complete')
        state=device.state();assert state['reasoning_header_count']>=1
        device.tap(state['reasoning_header_x'],state['reasoning_header_y'])
        assert device.state()['reasoning_expanded_count']==0
        device.capture('encrypted-reasoning-time');results.append('encrypted-time-only')
        # API default is a real slider stop and bypasses inherited strengths.
        device.tap(858,232);assert device.state()['effort_choices'][0]=='api_default'
        device.tap(146,110);device.tap(800,230);assert device.state()['effective_effort']=='api_default'
        mode('ok');send_request();wait_reply(11,'complete');assert requests()[-1]['effort']==''
        results.append('explicit-api-default')
        history_menu();device.capture('history-context');device.tap(178,112)
        device.wait(lambda:device.state()['keyboard']);device.tap(35,35)
        device.wait(lambda:device.state()['navigation_open']);assert session_info()[2]['title_state']=='complete'
        history_menu();device.tap(462,112);assert session_info()[2].get('pinned_at',0)>0
        history_menu();device.tap(178,172);assert session_info()[1]=='archived'
        device.tap(500,236);device.tap(740,80);device.view('archives');device.capture('archived-chat')
        device.tap(726,76);assert session_info()[1]=='active'
        device.to_chat();history_menu();device.tap(178,172)
        device.tap(500,236);device.tap(740,80);device.tap(846,76);assert session_info() is None
        results.append('history-long-press-rename-cancel-pin-archive-restore-delete')
        assert device.adb('pidof miniapp')==parent
    finally:
        # Finish bounded injected touches before restoring the user's database.
        for gesture in gesture_processes:gesture.wait(timeout=5)
        close_app();push(OUT/'before.db',DATA+'/penchat.db')
        after_files=set(device.adb('find '+DATA+'/DCIM '+DATA+'/assets -type f 2>/dev/null || true').splitlines())
        for path in after_files-before_files:device.adb('rm -f -- '+shlex.quote(path))
        fixture_requests=requests()
        device.adb('kill -TERM '+fixture_pid)
        fixture_process.wait(timeout=5)
        start()
        (OUT/'results.json').write_text(json.dumps({'passed':results,'requests':fixture_requests,'restored_database':True},indent=2)+'\n')
    print('ORANGE native chat: '+', '.join(results)+'; original DB restored.')
