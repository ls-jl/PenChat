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
        assert 'sc035gs' in sensor;profile='y02-sc035gs';shape=(640,480);capacity=375
    else:
        assert 'RK3562 ORANGE ' in board and 'sc2356' in sensor;profile='orange-sc2356';shape=(496,360);capacity=640
    def ready():
        s=wait(lambda:(d if (d:=state())['scanner'].get('type')=='ready' else None),timeout=20)
        assert s['scanner']['profile']==profile and (s['scanner']['sensor_width'],s['scanner']['sensor_height'])==shape
        return s
    def released():
        wait(lambda:state()['scan_released'],timeout=15)
        assert shell('cat /sys/class/gpio/gpio43/value')=='0'
        wait(lambda:shell('pidof CaptureFrame'),timeout=10)
    results=[]
    ready();c['capture']('scanner-ready-'+profile)
    for cycle in range(6):
        ready()
        p=subprocess.Popen(['adb','-s',c['SERIAL'],'shell','/tmp/penchat-display-key /dev/input/by-path/led_control 134 1 500 100'],stdout=subprocess.DEVNULL)
        assert p.wait(timeout=5)==0
        r=wait(lambda:(d['scan_result'] if (d:=state()).get('scan_result',{}).get('type')=='finished' else None),timeout=90)
        counts=r['counts'];assert r['profile']==profile and counts['received']>=5,r
        assert counts['discarded_backlog']==0 and counts['queue_capacity']==capacity,r
        assert counts['dropped_raw']==counts['recovered_gaps'],r
        if profile=='y02-sc035gs':assert r['captureEndedUs'] and r['lastSourceFrameUs']<=r['captureEndedUs'],r
        assert shell('cat /sys/class/gpio/gpio43/value')=='0'
        results.append({'received':counts['received'],'selected':counts['selected'],'batches':counts['batches'],'status':r['status'],'reason':r.get('reason','')})
        c['capture']('scanner-result-'+str(cycle))
        # Keep even a usable image out of the fixture's conversation.
        tap(W-205,108);wait(lambda:state()['view']=='chat');ready()
        snapshot=c['DATA']+'/scanner-acceptance.db'
        shell('sqlite3 -readonly '+shlex.quote(c['DATA']+'/penchat.db')+' '+shlex.quote('.backup '+snapshot))
        assert shell('sqlite3 -readonly '+shlex.quote(snapshot)+' '+shlex.quote('select count(*) from chat_sessions'))=='0'
        tap(40,32);wait(lambda:state()['navigation_open']);released()
        fmt=shell('v4l2-ctl -d /dev/video23 --get-fmt-video');assert fmt==c['scanner_profile_format'],fmt
        tap(W-30,80);ready()
    # Parent fallback must restore the saved Y02 format after an abrupt child exit.
    worker=shell('pidof penchat-scanner');assert worker.isdigit()
    shell('kill -KILL '+worker)
    tap(40,32);wait(lambda:state()['navigation_open']);released()
    assert shell('v4l2-ctl -d /dev/video23 --get-fmt-video')==c['scanner_profile_format']
    tap(W-30,80);ready();assert shell('pidof penchat-scanner')!=worker
    tap(W//2,H-34);wait(lambda:state()['editor']['active']);released()
    wait(lambda:state()['editor']['ocr_active']);c['capture']('scanner-keyboard-ocr-release')
    b=state()['editor']['actions']['cancel'];tap((b[0]+b[2])//2,(b[1]+b[3])//2);ready()
    shell('miniapp_cli start 8080222437664451');wait(lambda:not state()['visible']);released()
    shell('miniapp_cli start '+c['APP']['appid']);wait(lambda:state()['visible']);ready()
    c['result']['scanner_profile']={'profile':profile,'native_shape':shape,'queue_capacity':capacity,'cycles':results,'ocr_release':True,'background_release':True,'child_crash_restore':True,'discard_only':True,'paper_quality_validated':False}
