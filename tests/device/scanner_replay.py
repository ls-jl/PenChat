#!/usr/bin/env python3
"""Explicit ORANGE-only NPU replay. All input/output files stay on the host."""
import argparse, hashlib, json, pathlib, shlex, socket, struct, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[2]
SERIAL='MEB0400008900794'
BUNDLE=pathlib.Path('/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018')
def adb(*args,**kwargs):
    return subprocess.check_output(['adb','-s',SERIAL,*args],**kwargs)
def exact(peer,n):
    chunks=[]
    while n:
        b=peer.recv(min(n,65536))
        if not b:raise RuntimeError('truncated NPU result')
        chunks.append(b);n-=len(b)
    return b''.join(chunks)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--folder',type=pathlib.Path,required=True)
    p.add_argument('--expected-image',type=pathlib.Path)
    p.add_argument('--expected-mask',type=pathlib.Path)
    p.add_argument('--interval-us',type=int,default=40000)
    p.add_argument('--port',type=int,default=18881)
    a=p.parse_args()
    if a.interval_us<0 or not 1024<=a.port<=65535:p.error('invalid interval/port')
    from PIL import Image
    import numpy as np
    frames=sorted(a.folder.glob('frame*.png'))
    if not frames:p.error('no input frames')
    assert 'ORANGE' in adb('shell','cat /proc/device-tree/model',text=True)
    run=ROOT/'.work/scanner-replay'/str(time.time_ns());run.mkdir(parents=True)
    remote='/tmp/penchat-scan-replay-'+str(time.time_ns())
    # This directory only contains executable/model/runtime resources, never frames.
    adb('shell','mkdir -p '+shlex.quote(remote))
    adb('push',str(ROOT/'assets/scanner')+'/.',remote,stderr=subprocess.DEVNULL)
    adb('push',str(ROOT/'build/scanner-orange/scanner-replay'),remote+'/scanner-replay',stderr=subprocess.DEVNULL)
    adb('shell','chmod 755 '+shlex.quote(remote+'/scanner-replay'))
    forwarded=int(adb('forward','tcp:0','tcp:'+str(a.port),text=True).strip())
    log=(run/'runtime.log').open('wb')
    command='echo $$ > '+shlex.quote(remote+'/replay.pid')+'; exec env LD_LIBRARY_PATH='+shlex.quote(remote+'/lib')+' '+shlex.quote(remote+'/scanner-replay')+' rknn '+shlex.quote(remote+'/pintu_32.rknn')+' librknnrt.so --tcp '+str(a.port)
    process=subprocess.Popen(['adb','-s',SERIAL,'shell',command],stdout=log,stderr=log)
    peer=None
    try:
        end=time.monotonic()+10
        # adb forward accepts locally even before the remote listener exists.
        while time.monotonic()<end:
            if b'STREAM32_LISTEN' in (run/'runtime.log').read_bytes():break
            if process.poll() is not None:raise RuntimeError('NPU replay exited before listening')
            time.sleep(.05)
        else:raise RuntimeError('NPU replay did not listen')
        peer=socket.create_connection(('127.0.0.1',forwarded),timeout=3)
        peer.settimeout(120)
        peer.sendall(struct.pack('<4sII6I',b'PS32',1,1,496,360,3,len(frames),a.interval_us,0))
        peer.sendall((BUNDLE/'resources/illumination/demo_gain_160x288.f32').read_bytes())
        for path in frames:
            image=Image.open(path).convert('L')
            if image.size!=(496,360):raise RuntimeError('expected unrotated 496x360 Y frames')
            peer.sendall(image.tobytes())
        assert exact(peer,12)==b'PS32_RESULT\n'
        numbers=struct.unpack('<12Q',exact(peer,96))
        scan,status,x,y,w,h,received,selected,filtered,committed,unlocated,batches=numbers
        if not w or not h or w*h>128*1024*1024:raise RuntimeError('empty or oversized result')
        gray=np.empty((h,w),np.uint8);mask=np.empty_like(gray)
        for row in range(h):
            gray[row]=np.frombuffer(exact(peer,w),np.uint8)
            mask[row]=np.frombuffer(exact(peer,w),np.uint8)
        Image.fromarray(gray).save(run/'image.png');Image.fromarray(mask).save(run/'mask.png')
        result=dict(status=status,received=received,selected=selected,filtered=filtered,
            committed=committed,unlocated=unlocated,batches=batches,width=w,height=h,
            gray_sha256=hashlib.sha256(gray.tobytes()).hexdigest(),
            mask_sha256=hashlib.sha256(mask.tobytes()).hexdigest())
        if a.expected_image:
            expected=np.asarray(Image.open(a.expected_image).convert('L'))
            assert expected.shape==gray.shape
            difference=np.abs(gray.astype(np.int16)-expected.astype(np.int16))
            assert difference.max(initial=0)<=1
            result['maximum_gray_difference']=int(difference.max(initial=0))
        if a.expected_mask:
            assert np.array_equal(mask,np.asarray(Image.open(a.expected_mask).convert('L')))
            result['mask_equal']=True
        assert received==len(frames) and selected==committed+unlocated
        assert process.wait(timeout=10)==0
        (run/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result));print('Host artifacts:',run)
    finally:
        if peer:peer.close()
        if process.poll() is None:
            try:
                pid=adb('shell','cat '+shlex.quote(remote+'/replay.pid'),text=True).strip()
                if pid.isdigit():
                    cmd=adb('shell',"tr '\\0' ' ' < /proc/"+pid+'/cmdline',text=True)
                    if remote+'/scanner-replay' in cmd:adb('shell','kill -TERM '+pid)
            except subprocess.CalledProcessError:pass
            process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        log.close()
        adb('forward','--remove','tcp:'+str(forwarded))
        # Delete only this invocation's unique resource directory.
        adb('shell','rm -rf -- '+shlex.quote(remote))
if __name__=='__main__':main()
