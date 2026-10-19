#!/usr/bin/env python3
"""Isolated ORANGE camera/editor/gsensor check; restores user DB and files."""
import hashlib,json,pathlib,shlex,shutil,sqlite3,struct,subprocess,tempfile,time
import acceptance as d
from chat_acceptance import close_app
OUT=d.ROOT/'.work/device-photo';OUT.mkdir(parents=True,exist_ok=True)
def pull(remote,local):subprocess.run(['adb','-s',d.SERIAL,'pull',remote,str(local)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def push(local,remote):subprocess.run(['adb','-s',d.SERIAL,'push',str(local),remote],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def point(x,y,angle):return (y,266-x) if angle==90 else (960-x,266-y) if angle==180 else (960-y,x) if angle==270 else (x,y)
def tap(x,y,angle):d.tap(*point(x,y,angle))
def move(x,y,dx,dy,angle):
    px,py=point(x,y,angle);qx,qy=point(x+dx,y+dy,angle)
    d.adb('/tmp/penchat-ui-tap '+str(round(py+113))+' '+str(round(959-px))+' '+str(round(qy+113))+' '+str(round(959-qx))+' 25 2>/dev/null')
def start():
    d.adb('miniapp_cli start '+d.APP_ID)
    try:d.wait(d.pid)
    except AssertionError:
        # After the deliberate native SIGKILL, use the launcher's normal retry.
        d.tap(122,218);d.wait(d.pid)
    time.sleep(.5);d.view('chat')
def jpeg_size(data):
    i=2
    while i<len(data):
        while data[i]==255:i+=1
        marker=data[i];i+=1
        if marker in (0xd8,0xd9):continue
        length=int.from_bytes(data[i:i+2],'big')
        if marker in (0xc0,0xc1,0xc2):return (int.from_bytes(data[i+5:i+7],'big'),int.from_bytes(data[i+3:i+5],'big'))
        i+=length
    raise AssertionError('No JPEG dimensions')
if __name__=='__main__':
    s=d.state();assert s['view']=='chat' and not s['keyboard'] and not s['generating']
    host=d.adb('pidof miniapp');report=d.adb('cat /sys/kernel/gsensor/report_active');wake=d.adb('cat /sys/kernel/gsensor/wake_enable')
    close_app();pull(d.DATA+'/penchat.db',OUT/'before.db')
    files=set(d.adb('find '+d.DATA+'/assets '+d.DATA+'/DCIM -type f 2>/dev/null || true').splitlines())
    shutil.copy2(OUT/'before.db',OUT/'fixture.db')
    with sqlite3.connect(OUT/'fixture.db') as db:
        for key,value in {'camera_auto_orientation':False,'camera_orientation':0,'active_session_id':'photo-test-transient','penchat_settings':{'autoSendCapturedContent':False,'saveCameraToAlbum':False}}.items():
            db.execute('insert or replace into app_settings(key,value_json,updated_at) values(?,?,1)',(key,json.dumps(value)))
    results=[]
    try:
        push(OUT/'fixture.db',d.DATA+'/penchat.db');start()
        landscape=None
        for angle in [0,90,180,270]:
            d.tap(128,232);d.tap(200,38);d.view('camera')
            while d.state()['camera_angle']!=angle:
                current=d.state()['camera_angle'];tap(133 if current%180 else 888,934 if current%180 else 222,current)
                d.wait(lambda:d.state()['camera_angle']==(current+90)%360)
            d.wait(lambda:d.state()['camera_ready'],timeout=12)
            d.capture('camera-orientation-'+str(angle))
            tap(133 if angle%180 else 888,870 if angle%180 else 133,angle);d.view('crop')
            state=d.state();size=tuple(state['crop_source'])
            if landscape is None:landscape=size
            assert size==(landscape[::-1] if angle%180 else landscape),size
            source=d.adb('find '+d.DATA+'/camera-tmp -name "IMG*.jpg" -type f').splitlines()[0]
            with tempfile.TemporaryDirectory() as folder:
                path=pathlib.Path(folder)/'photo.jpg';pull(source,path);assert jpeg_size(path.read_bytes())==size
            pw,ph=state['crop_preview'];pan=state['crop_pan'];vx,vy=(44,68) if angle%180 else (98,44)
            move(vx+pan[0]+3,vy+pan[1]+3,pw-70,ph-65,angle)
            tap(222 if angle%180 else 922,738 if angle%180 else 64,angle)
            d.wait(lambda:d.state()['crop_zoom']>2)
            d.capture('photo-editor-'+str(angle))
            tap(70 if angle%180 else 794,934 if angle%180 else 230,angle);d.view('chat')
            assert not d.state()['generating']
            assert d.adb('cat /sys/kernel/gsensor/report_active')==report
            results.append({'angle':angle,'jpeg_size':size})
        # Enable the real sensor and verify its crash-safe ownership release.
        d.tap(128,232);d.tap(200,38);d.view('camera');angle=d.state()['camera_angle']
        tap(133 if angle%180 else 888,792 if angle%180 else 38,angle)
        d.wait(lambda:d.state()['camera_sensor'],timeout=12)
        assert d.adb('cat /sys/kernel/gsensor/report_active')=='1'
        assert d.adb('cat /sys/kernel/gsensor/wake_enable')==wake
        assert d.adb('pidof penchat-sensor-guard')
        # Capture freezes the pose and releases sampling before editing.
        d.wait(lambda:d.state()['camera_ready'],timeout=12)
        angle=d.state()['camera_angle']
        tap(133 if angle%180 else 888,870 if angle%180 else 133,angle);d.view('crop')
        d.wait(lambda:d.adb('cat /sys/kernel/gsensor/report_active')==report)
        d.wait(lambda:not d.adb('pidof penchat-sensor-guard || true'))
        tap(40,32,angle);d.view('chat')
        d.tap(128,232);d.tap(200,38);d.view('camera')
        d.wait(lambda:d.adb('cat /sys/kernel/gsensor/report_active')=='1',timeout=12)
        angle=d.state()['camera_angle'];tap(40,32,angle);d.view('chat')
        d.wait(lambda:d.adb('cat /sys/kernel/gsensor/report_active')==report)
        d.tap(128,232);d.tap(200,38);d.view('camera')
        d.wait(lambda:d.adb('cat /sys/kernel/gsensor/report_active')=='1',timeout=12)
        d.adb('kill -KILL '+d.pid());d.wait(lambda:not d.pid())
        d.wait(lambda:d.adb('cat /sys/kernel/gsensor/report_active')==report)
        d.wait(lambda:not d.adb('pidof penchat-sensor-guard || true'))
        assert d.adb('cat /sys/kernel/gsensor/wake_enable')==wake
        results.append({'sensor':'report restored after capture, page exit and native SIGKILL; wake unchanged'})
    finally:
        close_app();push(OUT/'before.db',d.DATA+'/penchat.db')
        after=set(d.adb('find '+d.DATA+'/assets '+d.DATA+'/DCIM -type f 2>/dev/null || true').splitlines())
        for path in after-files:d.adb('rm -f -- '+shlex.quote(path))
        start();pull(d.DATA+'/penchat.db',OUT/'restored.db')
        assert (OUT/'restored.db').read_bytes()==(OUT/'before.db').read_bytes()
        assert d.adb('pidof miniapp')==host
        for name in ['before.db','fixture.db','restored.db']:(OUT/name).unlink(missing_ok=True)
    (OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('ORANGE photo: four camera/editor poses, actual JPEG dimensions, zoom, pending attachments, sensor acquire/crash release and unchanged user DB passed.')
