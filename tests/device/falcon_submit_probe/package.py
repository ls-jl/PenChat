#!/usr/bin/env python3
"""Build an isolated S6 submit probe, never the production application."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent
BUILD = ROOT / 'build/submit-probe-cv1826'
STAGE = ROOT / 'build/submit-probe-package'
IDENTITY = STAGE / 'identity.json'


def main():
    STAGE.mkdir(parents=True, exist_ok=True)
    if not IDENTITY.exists():
        IDENTITY.write_text(json.dumps({'appid': '800' + str(uuid.uuid4().int % 10**13).zfill(13)}))
    appid = json.loads(IDENTITY.read_text())['appid']
    assert len(appid) == 16 and appid.startswith('800') and appid.isdecimal()
    assert appid != json.loads((ROOT / 'package.json').read_text())['appid']
    compiler = subprocess.check_output(['bash', '-c', 'source scripts/native-target.sh cv1826; printf "%s" "$PENCHAT_BUILD_CXX"'], cwd=ROOT, text=True)
    subprocess.run(['cmake', '-S', str(SOURCE), '-B', str(BUILD), '-DCMAKE_BUILD_TYPE=Release',
                    '-DCMAKE_SYSTEM_NAME=Linux', '-DCMAKE_SYSTEM_PROCESSOR=arm',
                    '-DCMAKE_CXX_COMPILER=' + compiler, '-DPENCHAT_SUBMIT_PROBE_APP_ID=' + appid], check=True)
    subprocess.run(['cmake', '--build', str(BUILD), '-j6'], check=True)
    for name in ('src', 'libs', 'assets'):
        shutil.rmtree(STAGE / name, ignore_errors=True)
    shutil.copytree(SOURCE / 'app', STAGE / 'src')
    shutil.copytree(ROOT / 'src/platform', STAGE / 'src/platform')
    (STAGE / 'package.json').write_text(json.dumps({
        'name': 'PenChat S6 Submit Probe', 'appid': appid, 'version': '0.0.1',
        'description': 'Isolated native submission and shared-memory compositor prototype',
        'single-js-bundle': False, 'quickjs': {'version': '20200705', 'bigNum': False},
        'dependencies': {'falcon-ui': '2.0.2'}}, indent=2))
    client = STAGE / 'assets/bin/penchat-composite-client'
    client.parent.mkdir(parents=True)
    shutil.copy2(BUILD / 'penchat-composite-client', client)
    strip = subprocess.check_output(['bash', '-c', 'source scripts/native-target.sh cv1826; printf "%s" "$PENCHAT_BUILD_STRIP"'], cwd=ROOT, text=True)
    subprocess.run([strip, '--strip-unneeded', str(client)], check=True)
    elf = client.read_bytes()
    assert elf[:6] == b'\x7fELF\x01\x01' and int.from_bytes(elf[18:20], 'little') == 40
    notices = STAGE / 'assets/licenses'
    notices.mkdir()
    for name in ['lvgl.txt', 'lvgl-COPYRIGHTS.md', 'json.txt', 'json11.txt',
                 'sdk-Apache-NOTICES.txt', 'quickjs.txt', 'GCC-Runtime-Library-Exception-3.1.txt']:
        shutil.copy2(ROOT / 'licenses' / name, notices / name)
    shutil.copy2(ROOT / 'LICENSE', notices / 'LICENSE')
    payload = BUILD / 'libjsapi_penchat_submit_probe.so'
    data = payload.read_bytes()
    assert data[:6] == b'\x7fELF\x01\x01' and int.from_bytes(data[18:20], 'little') == 40
    (STAGE / 'libs').mkdir()
    filename = 'libjsapi_penchat_submit_probe_' + str(int(hashlib.sha256(data).hexdigest()[:12], 16)) + '.so'
    shutil.copy2(payload, STAGE / 'libs' / filename)
    subprocess.run(['aiot-cli', '-c', '-q', '-p'], cwd=STAGE, check=True)
    package = STAGE / (appid + '.0_0_1.amr')
    assert package.is_file()
    print(package)


if __name__ == '__main__':
    main()
