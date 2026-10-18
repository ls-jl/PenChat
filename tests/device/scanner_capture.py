#!/usr/bin/env python3
"""ORANGE paired pen events and real sensor frames, always discard results.

This is a capture/cleanup check, not a paper-text quality acceptance.
"""
import json
import acceptance as d
from scanner_acceptance import ready, OUT

def main():
    assert 'ORANGE' in d.adb('cat /proc/device-tree/model')
    d.to_chat();ready()
    parent=d.adb('pidof miniapp')
    results=[]
    for i in range(20):
        ready()
        d.adb('/tmp/penchat-key /dev/input/by-path/led_control 134 1 500 100')
        d.view('scan')
        result=d.wait(lambda:(s['scan_result'] if (s:=d.state()).get('scan_result',{}).get('type')=='finished' else None),timeout=30)
        assert result['counts']['received']>=5,result
        assert result['counts']['dropped_raw']==0,result
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
        d.tap(840,210);d.view('chat');ready()
    assert d.adb('pidof miniapp')==parent
    assert not d.adb('find '+d.DATA+'/.scan-pending -type f 2>/dev/null || true')
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'capture.json').write_text(json.dumps(results,indent=2)+'\n')
    print('ORANGE: 20 real sensor captures, paired pen release, accounting, light-off and discard passed; paper quality remains separate.')

if __name__=='__main__':main()
