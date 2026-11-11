#!/usr/bin/env python3
"""Explicit RK runtime parity; synthetic frames stay in host RAM, no user DB access."""
import argparse
import hashlib
import json
import pathlib
import shlex
import socket
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGETS = {'MEB0400008900794': ('orange-sc2356', 496, 360),
           'MEA2300009007113': ('y02-sc035gs', 640, 480),
           '7G12000010905426': ('y07-sc2356', 752, 440)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, choices=TARGETS)
    parser.add_argument('--reference', choices=('system','current'),
                        help='Default: original system runtime; Y07 keeps its known-good private runtime.')
    args = parser.parse_args()
    serial = args.serial
    profile, width, height = TARGETS[serial]
    appid = json.loads((ROOT/'package.json').read_text())['appid']
    adb = ['adb', '-s', serial]
    out = ROOT/'.work/scanner-unification/runtime'/serial
    out.mkdir(parents=True, exist_ok=True)
    remote = '/tmp/penchat-runtime-parity-'+str(time.time_ns())

    def sh(command):
        return subprocess.check_output(adb+['shell', command], text=True).replace('\r', '').strip()

    def alive(name):
        result = []
        for pid in sh('pidof '+name+' || true').split():
            status = sh('cat /proc/'+pid+'/stat 2>/dev/null || true').rsplit(')', 1)[-1].split()
            if status and status[0] not in ('Z', 'X'):
                result.append(pid)
        return result

    def wait(fn, seconds=20):
        deadline = time.monotonic()+seconds
        while time.monotonic()<deadline:
            if fn():
                return
            time.sleep(.1)
        raise RuntimeError('Device transition timed out')

    board = sh('cat /proc/device-tree/model').replace('\0', '')
    sensor = sh('cat /sys/class/video4linux/v4l-subdev2/name')
    token = 'ORANGE' if profile.startswith('orange') else 'RK3562 Y02 ' if profile.startswith('y02') else 'RK3562 Y07 '
    assert token in board and ('sc035gs' if profile.startswith('y02') else 'sc2356') in sensor
    host = sh('pidof miniapp')
    boot = sh('cat /proc/sys/kernel/random/boot_id')
    owned = alive('penchat-lvgl')
    for pid in owned:
        assert appid in sh("tr '\\0' ' ' < /proc/"+pid+'/cmdline')
    report = {'serial': serial, 'profile': profile, 'fixture': 'synthetic_stationary_text',
              'paper_quality_validated': False, 'runs': {}, 'passed': False}
    result_path = out/'result.json'
    try:
        # Normal app shutdown releases its resident model and saves app-owned state.
        if owned:
            sh('miniapp_cli start 8080222437664451')
            for pid in owned:
                sh('kill -TERM '+pid)
            wait(lambda: not alive('penchat-lvgl'))
        wait(lambda: not alive('penchat-scanner'))
        sh('mkdir -p '+shlex.quote(remote))
        subprocess.run(adb+['push', str(ROOT/'assets/scanner')+'/.', remote], check=True, capture_output=True)
        subprocess.run(adb+['push', str(ROOT/'build/scanner-rk3562/scanner-replay'), remote+'/scanner-replay'], check=True, capture_output=True)
        sh('chmod 755 '+remote+'/scanner-replay')
        private = remote+'/lib/librknnrt-rk3562.so'
        report['private_sha256'] = sh('sha256sum '+private).split()[0]
        assert report['private_sha256'] == '9c5e43179e83f82b3ec1f2c2f9b520559af23ab76327aca63500db30d63d6910'
        # Upright text-like strokes, then native orientation (CCW90 in the engine).
        # Repeated input isolates runtime output from selection and hand movement.
        frame = bytearray(width*height)
        for y in range(height):
            for x in range(width):
                ux, uy = y, width-1-x
                ink = 12<ux<height-12 and uy%38 in range(9, 28) and (
                    ux%19 in (2, 3, 11, 12) or (uy%38 in (9, 10, 17, 18, 26, 27) and 2<=ux%19<=12))
                frame[y*width+x] = 38 if ink else 220
        gain = (ROOT/'assets/scanner/demo_gain_160x288.f32').read_bytes() if profile.startswith('orange') else struct.pack('<46080f', *([1.0]*46080))
        report['input_sha256'] = hashlib.sha256(frame).hexdigest()

        def exact(peer, count):
            data = bytearray()
            while len(data)<count:
                chunk = peer.recv(count-len(data))
                if not chunk:
                    raise RuntimeError('Truncated runtime replay result')
                data.extend(chunk)
            return data

        def replay(policy):
            port = int(subprocess.check_output(adb+['forward', 'tcp:0', 'tcp:18881'], text=True).strip())
            logpath = out/(policy+'.log')
            command = ('echo $$ > '+remote+'/replay.pid; exec env LD_LIBRARY_PATH='+shlex.quote(remote+'/lib')+
                       ' '+remote+'/scanner-replay profile '+profile+' '+remote+' '+policy+' --tcp 18881')
            peer = None
            with logpath.open('wb') as log:
                process = subprocess.Popen(adb+['shell', command], stdout=log, stderr=log)
                try:
                    wait(lambda: b'STREAM32_LISTEN' in logpath.read_bytes() or process.poll() is not None)
                    if process.poll() is not None:
                        raise RuntimeError('Replay exited before listening')
                    peer = socket.create_connection(('127.0.0.1', port), timeout=10)
                    peer.settimeout(90)
                    peer.sendall(struct.pack('<4sII6I', b'PS32', 1, 1, width, height, 3, 80, 10000, 1))
                    peer.sendall(gain)
                    for _ in range(80):
                        peer.sendall(frame)
                    assert exact(peer, 12) == b'PS32_RESULT\n'
                    values = struct.unpack('<12Q', exact(peer, 96))
                    scan, status, x, y, w, h, received, selected, filtered, committed, unlocated, batches = values
                    x = x-(1<<64) if x >= 1<<63 else x
                    y = y-(1<<64) if y >= 1<<63 else y
                    assert w and h and w*h<128*1024*1024
                    gray, mask = bytearray(), bytearray()
                    for _ in range(h):
                        gray.extend(exact(peer, w));mask.extend(exact(peer, w))
                    assert process.wait(timeout=15) == 0
                    assert b'REPLAY_WARMUP_US' in logpath.read_bytes()
                    assert received == selected == 80 and batches >= 3
                    result = dict(status=status, x=x, y=y, width=w, height=h, received=received,
                                  selected=selected, committed=committed, unlocated=unlocated, batches=batches,
                                  gray_sha256=hashlib.sha256(gray).hexdigest(), mask_sha256=hashlib.sha256(mask).hexdigest())
                    report['runs'][policy] = result
                    return result, gray, mask
                finally:
                    if peer:
                        peer.close()
                    if process.poll() is None:
                        pid = sh('cat '+remote+'/replay.pid')
                        if pid.isdigit() and remote+'/scanner-replay' in sh("tr '\\0' ' ' < /proc/"+pid+'/cmdline'):
                            sh('kill -TERM '+pid)
                        process.terminate()
                    process.wait(timeout=15)
                    subprocess.run(adb+['forward', '--remove', 'tcp:'+str(port)], check=True, capture_output=True)
        reference = args.reference or ('current' if profile.startswith('y07') else 'system')
        before, before_gray, before_mask = replay(reference)
        after, after_gray, after_mask = replay('private')
        fields = ('status', 'x', 'y', 'width', 'height', 'received', 'selected', 'committed', 'unlocated', 'batches')
        assert all(before[k] == after[k] for k in fields), (before, after)
        assert before_mask == after_mask
        report['maximum_gray_difference'] = max((abs(a-b) for a, b in zip(before_gray, after_gray)), default=0)
        assert report['maximum_gray_difference'] <= 1
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['host_unchanged'] = sh('pidof miniapp') == host and sh('cat /proc/sys/kernel/random/boot_id') == boot
        sh('rm -rf -- '+shlex.quote(remote))
        if owned:
            sh('miniapp_cli start '+appid)
        result_path.write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
