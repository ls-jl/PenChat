"""Real original-image sensor frames; results always discarded, never sent."""
import json,shlex,sqlite3,subprocess,sys,time

def prepare(root,folder):
    import shutil
    sys.path.insert(0,str(root/'tests'));from test_reasoning_ui import seed
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();seed(folder,1)
    with sqlite3.connect(folder/'penchat.db') as db:
        for key,value in [('auto_title_enabled',False),('auto_context_compression',False),('penchat_settings',{'autoSendCapturedContent':False})]:
            db.execute('insert or replace into app_settings values(?,?,1)',(key,json.dumps(value)))
    return folder/'penchat.db'

def run(c):
    state,wait,tap,shell=c['state'],c['wait'],c['tap'],c['shell'];W,H=c['W'],c['H']
    board=shell('cat /proc/device-tree/model');sensor=shell('cat /sys/class/video4linux/v4l-subdev2/name')
    if 'RK3562 Y02 ' in board:
        assert 'sc035gs' in sensor;profile='y02-sc035gs';shape=(640,480)
    elif 'RK3562 Y07 ' in board:
        assert 'sc2356' in sensor;profile='y07-sc2356';shape=(752,440)
    else:
        assert 'RK3562 ORANGE ' in board and 'sc2356' in sensor;profile='orange-sc2356';shape=(496,360)
    def ready():
        s=wait(lambda:(d if (d:=state())['scanner'].get('type')=='ready' else None),timeout=20)
        assert s['scanner']['profile']==profile and (s['scanner']['sensor_width'],s['scanner']['sensor_height'])==shape
        return s
    def cancel():tap(W//2,H-36) if H>W else tap(W-205,108)
    def light():return shell('cat '+('/sys/class/leds/flash-led-gpio/brightness' if profile=='y07-sc2356' else '/sys/class/gpio/gpio43/value'))
    def released():
        wait(lambda:state()['scan_released'],timeout=15)
        assert light()=='0'
        wait(lambda:shell('pidof CaptureFrame'),timeout=10)
    results=[]
    def press(hold):
        # This firmware's adbd may return zero even when the remote helper fails.
        output=subprocess.check_output(['adb','-s',c['SERIAL'],'shell',f'/tmp/penchat-display-key /dev/input/by-path/led_control 134 1 {hold} 100'],text=True)
        assert 'sent 1 paired events code=134' in output,output
    resident=ready()['scanner']['worker_pid'];assert str(resident)==shell('pidof penchat-scanner')
    c['capture']('scanner-ready-'+profile)
    empty_retry=False
    if profile in ('y02-sc035gs','y07-sc2356'):
        for attempt in range(3):
            press(1)
            empty=wait(lambda:(r if (r:=state().get('scan_result',{})).get('type')=='finished' else None),timeout=90)
            if not empty['usable']:
                c['capture']('scanner-empty-ready-to-retry')
                press(500)
                retry=wait(lambda:(r if (r:=state().get('scan_result',{})).get('type')=='finished' and r['scanId']!=empty['scanId'] else None),timeout=90)
                assert retry['counts']['received']>=5 and shell('pidof penchat-scanner')==str(resident)
                empty_retry=True;c['capture']('scanner-retried-without-leaving')
            cancel();wait(lambda:state()['view']=='chat');ready()
            if empty_retry:break
        assert empty_retry,'Could not exercise an empty result with a short physical press'
    for cycle in range(20):
        assert ready()['scanner']['worker_pid']==resident
        press(500)
        r=wait(lambda:(d['scan_result'] if (d:=state()).get('scan_result',{}).get('type')=='finished' else None),timeout=90)
        counts=r['counts'];assert r['profile']==profile and counts['received']>=5,r
        assert counts['discarded_backlog']==0 and counts['spilled']==0 and counts['system_reserve_bytes']>=32*1024*1024,r
        assert counts['dropped_raw']==counts['recovered_frames'],r
        if profile=='y02-sc035gs':assert r['captureEndedUs'] and r['lastSourceFrameUs']<=r['captureEndedUs'],r
        assert light()=='0'
        results.append({'received':counts['received'],'selected':counts['selected'],'batches':counts['batches'],'status':r['status'],'reason':r.get('reason','')})
        c['capture']('scanner-result-'+str(cycle))
        # Keep even a usable image out of the fixture's conversation.
        cancel();wait(lambda:state()['view']=='chat')
        assert ready()['scanner']['generation']==r['generation']
        snapshot=c['DATA']+'/scanner-acceptance.db'
        shell('sqlite3 -readonly '+shlex.quote(c['DATA']+'/penchat.db')+' '+shlex.quote('.backup '+snapshot))
        assert shell('sqlite3 -readonly '+shlex.quote(snapshot)+' '+shlex.quote('select count(*) from chat_sessions'))=='0'
        tap(40,32);wait(lambda:state()['navigation_open']);released()
        assert shell('pidof penchat-scanner')==str(resident)
        fmt=shell('v4l2-ctl -d /dev/video23 --get-fmt-video');assert fmt==c['scanner_profile_format'],fmt
        if cycle==0:
            handles=lambda:[line.split(' -> ',1)[-1] for line in shell(f'ls -l /proc/{resident}/fd').splitlines() if ' -> /dev/dri/' in line or ' -> /dev/rknpu' in line]
            npu_handles=handles();assert npu_handles,'Resident RKNN context has no device handle'
            shell('miniapp_cli start 8080222437664451');wait(lambda:not state()['visible'])
            time.sleep(6) # The previous model idle limit was five seconds.
            assert shell('pidof penchat-scanner')==str(resident) and handles()==npu_handles
            c['wake']();shell('miniapp_cli start '+c['APP']['appid']);wait(lambda:state()['visible'])
        else:
            tap(W-30,80)
        assert ready()['scanner']['worker_pid']==resident
    # Parent fallback must restore the saved Y02 format after an abrupt child exit.
    worker=shell('pidof penchat-scanner');assert worker.isdigit()
    shell('kill -KILL '+worker)
    tap(40,32);wait(lambda:state()['navigation_open']);released()
    assert shell('v4l2-ctl -d /dev/video23 --get-fmt-video')==c['scanner_profile_format']
    tap(W-30,80);resident=ready()['scanner']['worker_pid'];assert str(resident)!=worker
    tap(W//2,H-34);wait(lambda:state()['editor']['active']);released()
    wait(lambda:state()['editor']['ocr_active']);c['capture']('scanner-keyboard-ocr-release')
    assert shell('pidof penchat-scanner')==str(resident)
    b=state()['editor']['actions']['cancel'];tap((b[0]+b[2])//2,(b[1]+b[3])//2);ready()
    shell('miniapp_cli start 8080222437664451');wait(lambda:not state()['visible']);released()
    assert shell('pidof penchat-scanner')==str(resident)
    shell('miniapp_cli start '+c['APP']['appid']);wait(lambda:state()['visible']);ready()
    c['result']['scanner_profile']={'profile':profile,'native_shape':shape,'temporary_frames':'memory_only','cycles':results,'resident_worker':True,'idle_npu_handles':npu_handles,'empty_result_tip_retry':empty_retry,'ocr_release':True,'background_release':True,'child_crash_restore':True,'discard_only':True,'paper_quality_validated':False}
