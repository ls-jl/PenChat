#!/usr/bin/env python3
"""Package current ARM32 builds in an isolated development directory; no release."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from package_identity import amr_name

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ui-only', action='store_true', help='Isolate UI debugging without activating capture hardware')
    parser.add_argument('--scan-trace', action='store_true', help='Development-only, explicitly armed one-scan raw diagnostic')
    parser.add_argument('--no-build', action='store_true', help='Repackage verified existing target binaries without compiling')
    args = parser.parse_args()
    app = json.loads((ROOT / 'package.json').read_text())
    if not args.no_build:
        subprocess.run(['cmake', '-S', str(ROOT / 'native/scanner'), '-B', str(ROOT / 'build/scanner-cv1826'),
                        '-DSCANNER_DIAGNOSTIC_TRACE=' + ('ON' if args.scan_trace else 'OFF')], check=True)
        builds = [('cv1826', ['penchat-lvgl', 'penchat-sensor-guard', 'jsapi_penchat_launcher'])]
        if not args.ui_only:
            builds.append(('scanner-cv1826', ['penchat-scanner', 'penchat_cvi_media']))
        for directory, targets in builds:
            subprocess.run(['cmake', '--build', str(ROOT / 'build' / directory), '--target'] + targets + ['-j6'], check=True)

    stage = ROOT / ('build/package-cv1826-ui' if args.ui_only else 'build/package-cv1826')
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    for name in ['package.json', app['icon']]:
        shutil.copy2(ROOT / name, stage / name)
    shutil.copytree(ROOT / 'src', stage / 'src')
    shutil.copytree(ROOT / 'assets', stage / 'assets', ignore=shutil.ignore_patterns('bin', 'scanner'))
    bin_dir = stage / 'assets/bin'
    bin_dir.mkdir()
    for name, build in [('penchat-lvgl', 'cv1826'), ('penchat-sensor-guard', 'cv1826'), ('penchat-scanner', 'scanner-cv1826')]:
        if args.ui_only and name == 'penchat-scanner':
            continue
        shutil.copy2(ROOT / 'build' / build / name, bin_dir / name)
    scanner = stage / 'assets/scanner'
    subprocess.run(['python3', str(ROOT / 'scripts/prepare-scanner-cvi.py'), '--stage', str(scanner)], check=True)
    shutil.copy2(ROOT / 'native/scanner/cvi-media.lock.json', scanner / 'cvi-media.lock.json')
    for name, source in [('libpng-LICENSE', 'libpng.txt'), ('OpenCV-NOTICE', 'opencv-3.4.3.txt'),
                         ('zlib-NOTICE', 'zlib.txt'), ('libstdc++-NOTICE', 'GCC-Runtime-Library-Exception-3.1.txt')]:
        shutil.copy2(ROOT / 'licenses' / source, scanner / name)
    media = scanner / 'lib'
    media.mkdir()
    for name in ['libpng16.so.16', 'libz.so.1']:
        shutil.copy2(ROOT / '.deps/cv1826-png/lib' / name, media / name)
    shutil.copy2(ROOT / 'build/scanner-cv1826/libpenchat_cvi_media.so', media / 'libpenchat_cvi_media.so')
    if args.ui_only:
        shutil.rmtree(scanner)
    launcher = stage / 'libs/arm32-cv1826/libjsapi_penchat_launcher.so'
    launcher.parent.mkdir(parents=True)
    shutil.copy2(ROOT / 'build/cv1826/libjsapi_penchat_launcher.so', launcher)
    strip = subprocess.check_output(['bash', '-c', 'source scripts/native-target.sh cv1826; printf "%s" "$PENCHAT_BUILD_STRIP"'], cwd=ROOT).decode()
    for path in stage.rglob('*'):
        if path.is_file() and path.read_bytes()[:4] == b'\x7fELF':
            data = path.read_bytes()
            if data[4:6] != b'\x01\x01' or int.from_bytes(data[18:20], 'little') != 40:
                raise ValueError('Wrong native architecture: ' + str(path))
            subprocess.run([strip, '--strip-unneeded', str(path)], check=True)
    loader_id = int(hashlib.sha256(launcher.read_bytes()).hexdigest()[:12], 16)
    shutil.copy2(launcher, stage / 'libs' / ('libjsapi_penchat_launcher_' + str(loader_id) + '.so'))
    subprocess.run(['aiot-cli', '-c', '-q', '-p'], cwd=stage, check=True)
    package = stage / amr_name(app, 'cv1826')
    (stage / amr_name(app)).replace(package)
    subprocess.run(['python3', str(ROOT / 'tests/check_cv1826_amr.py'), str(package)] + (['--ui-only'] if args.ui_only else []), check=True)
    print(package)


if __name__ == '__main__':
    main()
