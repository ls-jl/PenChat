#!/usr/bin/env python3
"""Opt-in framebuffer/camera regression, never touches application data."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True, choices=(
    'MEB0400008900794', 'MEA2300009007113', '7G12000010905426'))
args = parser.parse_args()
adb = ['adb', '-s', args.serial]


def shell(command):
    return subprocess.check_output(adb + ['shell', command], text=True,
                                   timeout=30).replace('\r', '').strip()


def idle():
    assert not shell('pidof penchat-lvgl'), 'PenChat is active; preserve its state'
    assert not shell('pidof penchat-scanner'), 'Scanner is active; preserve its state'


idle()
host = shell('pidof miniapp')
remote = '/tmp/penchat-display-probe-' + str(time.time_ns())
output = ROOT / '.work' / 'audit-0.2.32'
output.mkdir(parents=True, exist_ok=True)
try:
    shell('mkdir ' + shlex.quote(remote))
    for binary in ('penchat-display-device-probe', 'penchat-display-pixel-tests'):
        subprocess.run(adb + ['push', str(ROOT / 'build/orange' / binary),
                              remote + '/' + binary], check=True,
                       capture_output=True, timeout=30)
    shell('chmod 700 ' + shlex.quote(remote) + '/*')
    idle()
    result = shell(remote + '/penchat-display-device-probe 2>&1')
    (output / (args.serial + '-display-probe.log')).write_text(result + '\n')
    if 'SKIP: selected overlay is already occupied' in result:
        print(args.serial + ': occupied overlay, left intact')
    else:
        report = next(json.loads(line) for line in result.splitlines()
                      if line.startswith('{') and 'drm_pixel_differences' in line)
        assert report['restored'] and report['ownership_checks']
        assert report['drm_pixel_differences'] == 0
        print(args.serial + ': ' + json.dumps(report, ensure_ascii=False))
    benchmark = shell(remote + '/penchat-display-pixel-tests 2>&1')
    assert 'DISPLAY_PIXEL_CONVERSION_OK' in benchmark
    (output / (args.serial + '-display-timings.log')).write_text(benchmark + '\n')
    assert shell('pidof miniapp') == host, 'MiniApp host changed'
    idle()
finally:
    shell('rm -rf ' + shlex.quote(remote))
