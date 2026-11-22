#!/usr/bin/env python3
"""Stage hash-locked scanner assets; never copy research or input captures."""
import argparse, hashlib, json, pathlib, shutil, os
import urllib.request
from scanner_headers import PNG_HEADERS, ZLIB_HEADERS, source_headers
ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT = pathlib.Path('/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018')
p = argparse.ArgumentParser()
p.add_argument('--bundle', type=pathlib.Path, default=os.environ.get('PENCHAT_SCAN_BUNDLE') or DEFAULT,
               help='RK scan resources (default: PENCHAT_SCAN_BUNDLE)')
p.add_argument('--stage', action='store_true')
p.add_argument('--cross', action='store_true')
a = p.parse_args()
lock = json.loads((ROOT/'native/scanner/dependencies.lock.json').read_text())
sdk = os.environ.get('RK_SDK_ROOT')
cross = pathlib.Path(os.environ.get('PENCHAT_SCAN_CROSS_ROOT','/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/.deps/arm-cross'))
board = pathlib.Path(os.environ.get('PENCHAT_SCAN_BOARD_LIBS',str(cross/'board-libs')))
if a.cross or a.stage:
    external = pathlib.Path(sdk)/'external' if sdk else None
    png = source_headers('libpng', external/'libpng' if external else None)
    zlib = source_headers('zlib', external/'zlib' if external else None)
    private=ROOT/'.deps/scanner-png'
    (private/'lib').mkdir(parents=True,exist_ok=True)
    (private/'include').mkdir(exist_ok=True)
    for name in PNG_HEADERS:
        shutil.copy2(png/name,private/'include'/name)
    for name in ZLIB_HEADERS:
        shutil.copy2(zlib/name,private/'include'/name)
    for name,entry in lock['runtime'].items():
        if entry['root']=='download':
            source=ROOT/'.deps/scanner-rknn'/entry['source']
            if not source.exists():
                source.parent.mkdir(parents=True,exist_ok=True)
                temporary=source.with_suffix('.download')
                try:
                    with urllib.request.urlopen(entry['url'],timeout=60) as response,temporary.open('wb') as output:
                        size=0
                        while chunk:=response.read(65536):
                            size+=len(chunk)
                            if size>16*1024*1024:raise ValueError('Scanner runtime download exceeds limit')
                            output.write(chunk)
                    assert hashlib.sha256(temporary.read_bytes()).hexdigest()==entry['sha256'],name
                    temporary.replace(source)
                finally:temporary.unlink(missing_ok=True)
        else:
            source=({'bundle':a.bundle,'board':board}[entry['root']])/entry['source']
        assert hashlib.sha256(source.read_bytes()).hexdigest()==entry['sha256'], name
        shutil.copy2(source,private/'lib'/name)
    # Linker name stays outside the production package.
    link=private/'lib/libstdc++.so'
    link.unlink(missing_ok=True);link.symlink_to('libstdc++.so.6')
for name, digest in lock['files'].items():
    assert hashlib.sha256((ROOT/'native/scanner'/name).read_bytes()).hexdigest() == digest, name
target = ROOT/'assets/scanner'
if a.stage: target.mkdir(parents=True, exist_ok=True)
for name, entry in lock['resources'].items():
    source = (ROOT/'native/scanner' if entry.get('root')=='project' else a.bundle)/entry['source']
    assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256'], name
    if a.stage: shutil.copy2(source, target/name)
if a.stage:
    # Retire the old target-specific name when rebuilding a generated stage.
    (target/'lib/librknnrt-y07.so').unlink(missing_ok=True)
    shutil.copy2(ROOT/'native/scanner/NOTICE', target/'NOTICE')
    shutil.copy2(ROOT/'native/scanner/dependencies.lock.json', target/'dependencies.lock.json')
    (target/'lib').mkdir(exist_ok=True)
    for name in lock['runtime']: shutil.copy2(private/'lib'/name,target/'lib'/name)
    shutil.copy2(png/'LICENSE',target/'libpng-LICENSE')
    shutil.copy2(ROOT/'licenses/opencv-3.4.3.txt',target/'OpenCV-NOTICE')
    shutil.copy2(zlib/'LICENSE',target/'zlib-NOTICE')
    shutil.copy2(ROOT/'licenses/GCC-Runtime-Library-Exception-3.1.txt',target/'libstdc++-NOTICE')
print('Stream32 pinned sources and model/calibration hashes verified.')
