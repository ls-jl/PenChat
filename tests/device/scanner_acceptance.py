#!/usr/bin/env python3
"""ORANGE real input/scan ownership cycles; never changes chat data."""
import argparse, json, time
import acceptance as d

OUT=d.ROOT/'.work/scanner-device'

def ready():
    previous=None
    def poll():
        nonlocal previous
        v=d.state();scanner=v.get('scanner',{})
        current=(scanner.get('generation'),scanner.get('type'))
        if current!=previous:
            OUT.mkdir(parents=True,exist_ok=True)
            with (OUT/'transitions.jsonl').open('a') as log:
                log.write(json.dumps({'time':time.time(),'scanner':scanner,'view':v['view'],'touch':v.get('touch'),'keyboard':v['keyboard'],'navigation':v['navigation_open']})+'\n')
            previous=current
        if scanner.get('type') in ('error','fatal'):
            raise RuntimeError('Scanner preparation failed: '+json.dumps(scanner))
        return v if scanner.get('type')=='ready' else None
    s=d.wait(poll,timeout=15)
    assert d.adb('cat /sys/class/gpio/gpio43/value')=='0'
    worker=d.adb('pidof penchat-scanner');assert worker.isdigit()
    return s

def released():
    d.wait(lambda:d.state()['scan_released'],timeout=10)
    assert d.adb('cat /sys/class/gpio/gpio43/value')=='0'
    d.wait(lambda:d.adb('pidof CaptureFrame || true'),timeout=10)

def reopen():
    d.adb('miniapp_cli start '+d.APP_ID);d.wait(d.pid);d.view('chat');ready()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cycles',type=int,default=20)
    parser.add_argument('--reentry-only',action='store_true')
    args=parser.parse_args()
    if not 1<=args.cycles<=100:parser.error('cycles must be 1..100')
    assert 'ORANGE' in d.adb('cat /proc/device-tree/model')
    OUT.mkdir(parents=True,exist_ok=True)
    parent=d.adb('pidof miniapp')
    d.to_chat();ready()
    result={'serial':d.SERIAL,'host_pid':parent,'cycles':[],'reentry_only':args.reentry_only}
    for i in range(args.cycles):
        if not args.reentry_only:
            d.open_navigation();released()
            d.tap(800,80);ready()
            d.open_settings();released();d.to_chat();ready()
            d.tap(128,232);released()
            d.tap(200,38);d.view('camera');released()
            d.tap(40,32);d.view('chat');ready()
            d.tap(128,232);d.tap(200,82);d.view('album');released()
            d.tap(40,32);d.view('chat');ready()
            d.tap(500,232);d.wait(lambda:d.state()['keyboard']);released()
            d.tap(35,35);d.wait(lambda:not d.state()['keyboard']);ready()
        d.home()
        d.wait(lambda:not d.adb('pidof penchat-scanner || true'),timeout=10)
        assert d.adb('cat /sys/class/gpio/gpio43/value')=='0'
        d.wait(lambda:d.adb('pidof CaptureFrame || true'),timeout=10)
        reopen()
        assert d.adb('pidof miniapp')==parent
        p=d.adb('pidof penchat-scanner')
        entry={'cycle':i+1,'scanner_pid':p,'fds':int(d.adb('ls /proc/'+p+'/fd | wc -l')),'rss_kib':int(d.adb("awk '/VmRSS/{print $2}' /proc/"+p+'/status'))}
        result['cycles'].append(entry);print(json.dumps(entry),flush=True)
    # System OCR retains its own bridge and releases the custom capture lease.
    d.open_settings();d.tap(180,80);d.view('provider')
    d.tap(790,30);d.tap(470,122);d.view('keyscan');released()
    d.tap(40,32);d.view('provider');d.to_chat();ready()
    result['system_ocr_release']=True
    # Kill only the scanner child: parent fallback restores the guardian/light.
    worker=d.adb('pidof penchat-scanner');d.adb('kill -KILL '+worker)
    d.wait(lambda:d.state()['scanner'].get('type')=='error',timeout=10)
    released();d.tap(40,100);ready()
    result['child_crash_recovery']=True
    d.adb('miniapp_cli start 8080222437664451')
    d.wait(lambda:not d.pid() or not d.state()['visible'])
    d.wait(lambda:not d.adb('pidof penchat-scanner || true'),timeout=10)
    assert d.adb('cat /sys/class/gpio/gpio43/value')=='0'
    reopen();result['background_resume']=True
    assert d.adb('pidof miniapp')==parent
    assert not d.adb('find '+d.DATA+'/.scan-pending -type f 2>/dev/null || true')
    (OUT/('lifecycle-reentry.json' if args.reentry_only else 'lifecycle.json')).write_text(json.dumps(result,indent=2)+'\n')
    print('ORANGE scanner:',args.cycles,'reentry' if args.reentry_only else 'menu/settings/camera/album/IME/Home','cycles, system OCR, child crash and background recovery passed.')

if __name__=='__main__':main()
