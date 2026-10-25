#!/usr/bin/env python3
"""Check license provenance and, when supplied, exact packaged notices."""
import argparse
import hashlib
import json
import pathlib
import runpy
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--amr', type=pathlib.Path)
args = parser.parse_args()
payload = runpy.run_path(str(ROOT / 'scripts/stage-licenses.py'))['license_payload']()
app = json.loads((ROOT / 'package.json').read_text())
lock = json.loads((ROOT / 'package-lock.json').read_text())
assert app['license'] == lock['packages']['']['license'] == 'Apache-2.0'
assert app['version'] == lock['version'] == lock['packages']['']['version']
assert 'Apache License' in (ROOT / 'LICENSE').read_text()
notice = (ROOT / 'NOTICE').read_text()
assert 'NOT obtained authorization' in notice and 'remaining SDK files has not been established' in notice
# Catch upstream updates without silently carrying forward an older notice.
for item in json.loads((ROOT / 'licenses/manifest.json').read_text())['files']:
    source = item['source']
    if source.startswith('.deps/') and (ROOT / source).is_file():
        assert hashlib.sha256((ROOT / source).read_bytes()).hexdigest() == item['sha256'], source
adaptation = json.loads((ROOT / 'native/camera/stb-adaptation.json').read_text())
assert hashlib.sha256((ROOT / 'native/camera' / adaptation['file']).read_bytes()).hexdigest() == adaptation['adapted_sha256']
if args.amr:
    with zipfile.ZipFile(args.amr) as archive:
        expected = {'assets/licenses/' + name for name in payload}
        actual = {n for n in archive.namelist() if n.startswith('assets/licenses/') and not n.endswith('/')}
        assert actual == expected, (actual - expected, expected - actual)
        for name, path in payload.items():
            assert archive.read('assets/licenses/' + name) == path.read_bytes(), name
        assert archive.read('assets/scanner/OpenCV-NOTICE') == payload['opencv-3.4.3.txt'].read_bytes()
        assert archive.read('assets/scanner/libstdc++-NOTICE') == payload['GCC-Runtime-Library-Exception-3.1.txt'].read_bytes()
print(f'License integrity passed: {len(payload)} files; this does not certify unresolved permissions.')
