#!/usr/bin/env python3
"""Targeted ARM32 development package identity, architecture and isolation check."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ui_only = '--ui-only' in sys.argv[2:]
app = json.loads((ROOT / 'package.json').read_text())
compositor_lock = json.loads((ROOT / 'native/compositor/source-lock.json').read_text())
for relative, digest in compositor_lock['adapters'].items():
    assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == digest, relative

with zipfile.ZipFile(sys.argv[1]) as archive:
    names = archive.namelist()
    assert len(names) == len(set(names))
    assert archive.testzip() is None
    manifest = json.loads(archive.read('manifest.json'))
    assert (manifest['appName'], manifest['appid'], manifest['version']) == (app['name'], app['appid'], app['version'])
    assert all(not Path(n).is_absolute() and '..' not in Path(n).parts for n in names)
    assert not any(n.endswith('.rknn') or 'arm64' in n or 'arm32-s6' not in n and n.startswith('libs/arm') for n in names)
    assert {n for n in names if n.startswith('assets/bin/') and not n.endswith('/')} == {
        'assets/bin/penchat-lvgl', 'assets/bin/penchat-sensor-guard'} | (set() if ui_only else {'assets/bin/penchat-scanner'})
    forbidden = set(json.loads((ROOT / 'native/scanner/cvi-media.lock.json').read_text())['libraries'])
    forbidden.update(['libcamera.so', 'libsamplevio.so', 'libcviruntime.so', 'libcvikernel.so'])
    count = 0
    for n in names:
        if n.endswith('/'):
            continue
        assert Path(n).name not in forbidden, 'Device-only SDK redistributed: ' + n
        data = archive.read(n)
        if data.startswith(b'\x7fELF'):
            assert data[4:6] == b'\x01\x01' and struct.unpack_from('<H', data, 18)[0] == 40, n
            assert struct.unpack_from('<I', data, 36)[0] & 0x400, 'Not hard-float: ' + n
            count += 1
    assert count == (4 if ui_only else 8), ('Unexpected native payload count', count)
    if not ui_only:
        model = archive.read('assets/scanner/pintu_rgb32_int8.cvimodel')
        lock = json.loads((ROOT / 'native/scanner/cvi-dependencies.lock.json').read_text())
        assert hashlib.sha256(model).hexdigest() == lock['files']['models/cvimodel/pintu_rgb32_int8_experimental.cvimodel']['sha256']
    binary = archive.read('assets/bin/penchat-lvgl')
    assert b'PENCHAT_TEST_CA' not in binary and b'scanner.test' not in binary
    assert b'S6 compositor is unavailable' in binary
    assert b'/sys/module/cvifb/parameters/start_' not in binary, 'Old CVI display linked'
    launcher = archive.read('libs/arm32-s6/libjsapi_penchat_launcher.so')
    assert b'11e46687d3c7301d3049d02d84144ee37e5a9994ca5ec1e9f5eaf278db92519b' in launcher
    assert b'--compositor-fd' in launcher
    assert 'assets/scanner/demo_gain.json' not in names
    assert ui_only or 'assets/scanner/NOTICE' in names
print('S6_ARM32_PACKAGE_IDENTITY_ARCHITECTURE_AND_ISOLATION_OK')
