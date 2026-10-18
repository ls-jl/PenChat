#!/usr/bin/env python3
"""ORANGE paired pen events and real sensor frames, always discard results.

This is a capture/cleanup check, not a paper-text quality acceptance.
"""
import argparse, json, os, subprocess, time
import acceptance as d
from scanner_acceptance import ready, OUT

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cycles',type=int,default=20)
    parser.add_argument('--hold-ms',type=int,default=500)
    args=parser.parse_args()
    assert 1<=args.cycles<=20 and 100<=args.hold_ms<=10000
    sdk=os.environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK')
    toolchain=os.environ.get('JSAPI_TOOLCHAIN_ROOT',sdk+'/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu')
    helper=d.ROOT/'.work/penchat-key'
    subprocess.run([toolchain+'/bin/aarch64-linux-gnu-g++','-std=c++11','-Os',str(d.ROOT/'tests/device/key.cpp'),'-o',str(helper)],check=True)
    subprocess.run(['adb','-s',d.SERIAL,'push',str(helper),'/tmp/penchat-key'],check=True,stdout=subprocess.DEVNULL)
    d.adb('chmod 755 /tmp/penchat-key')
    assert 'ORANGE'  in d.adb('cat /proc/device-tree/model')
    d.to_chat();ready()
    parent=d.adb('pidof miniapp')
    results=[]
    for i in range(args.cycles):
        ready()
        gesture=subprocess.Popen(['adb','-s',d.SERIAL,'shell','/tmp/penchat-key /dev/input/by-path/led_control 134 1 '+str(args.hold_ms)+' 100'],stdout=subprocess.DEVNULL)
        try:
            if args.hold_ms>=2000:
                time.sleep((args.hold_ms-200)/1000)
                state=d.state();assert state['view']=='scan' and not state.get('scan_result'),state.get('scan_result')
        finally:gesture.wait(timeout=15)
        assert gesture.returncode==0
        d.view('scan')
        result=d.wait(lambda:(s['scan_result'] if (s:=d.state()).get('scan_result',{}).get('type')=='finished' else None),timeout=90)
        assert result['counts']['received']>=5,result
        assert result['counts']['dropped_raw']==result['counts'].get('recovered_gaps',0),result
        assert result['counts'].get('discarded_backlog',0)==0,result
        assert result.get('reason','')=='',result
        assert result['firstFrameUs']>=result['pressedUs']
        assert 0<=result['lightRequestedUs']-result['pressedUs']<10000
        worker=d.adb('pidof penchat-scanner')
        rss=int(d.adb("awk '/VmHWM/{print $2}' /proc/"+worker+'/status'))
        assert rss<256*1024
        result['worker_hwm_kib']=rss
        results.append(result)
        OUT.mkdir(parents=True,exist_ok=True)
        (OUT/'capture.json').write_text(json.dumps(results,indent=2)+'\n')
        print(json.dumps({'scan':i+1,'counts':result['counts'],'hwm_kib':rss}),flush=True)
        assert d.adb('cat /sys/class/gpio/gpio43/value')=='0'
        d.tap(840,156);d.view('chat');ready()
    assert d.adb('pidof miniapp')==parent
    assert not d.adb('find '+d.DATA+'/.scan-pending -type f 2>/dev/null || true')
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'capture.json').write_text(json.dumps(results,indent=2)+'\n')
    print('ORANGE:',args.cycles,'real sensor captures, paired pen release, accounting, light-off and discard passed; paper quality remains separate.')

if __name__=='__main__':main()
