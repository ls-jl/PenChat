#!/usr/bin/env python3
"""Stage hash-locked scanner assets; never copy research or input captures."""
import argparse, hashlib, json, pathlib, shutil, os
ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT = pathlib.Path('/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/deliverables/stream32_complete_20261018')
p = argparse.ArgumentParser()
p.add_argument('--bundle', type=pathlib.Path, default=DEFAULT)
p.add_argument('--stage', action='store_true')
p.add_argument('--cross', action='store_true')
a = p.parse_args()
lock = json.loads((ROOT/'native/scanner/dependencies.lock.json').read_text())
sdk = pathlib.Path(os.environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK'))/'external'
cross = pathlib.Path(os.environ.get('PENCHAT_SCAN_CROSS_ROOT','/home/ls-jl/Documents/ChatGPT/词典笔扫描拼接/.deps/arm-cross'))
board = pathlib.Path(os.environ.get('PENCHAT_SCAN_BOARD_LIBS',str(cross/'board-libs')))
if a.cross or a.stage:
    for name,digest in lock['png_source'].items():
        assert hashlib.sha256((sdk/'libpng'/name).read_bytes()).hexdigest()==digest, name
    private=ROOT/'.deps/scanner-png'
    (private/'lib').mkdir(parents=True,exist_ok=True)
    (private/'include').mkdir(exist_ok=True)
    for name in ['png.h','pngconf.h','pnglibconf.h']:
        shutil.copy2(sdk/'libpng'/name,private/'include'/name)
    for name in ['zlib.h','zconf.h']:
        shutil.copy2(sdk/'zlib'/name,private/'include'/name)
    for name,entry in lock['runtime'].items():
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
    source = a.bundle/entry['source']
    assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256'], name
    if a.stage: shutil.copy2(source, target/name)
if a.stage:
    shutil.copy2(ROOT/'native/scanner/NOTICE', target/'NOTICE')
    shutil.copy2(ROOT/'native/scanner/dependencies.lock.json', target/'dependencies.lock.json')
    (target/'lib').mkdir(exist_ok=True)
    for name in lock['runtime']: shutil.copy2(private/'lib'/name,target/'lib'/name)
    shutil.copy2(sdk/'libpng/LICENSE',target/'libpng-LICENSE')
    shutil.copy2(a.bundle/'third_party/licenses/libopencv-core406/copyright',target/'OpenCV-NOTICE')
    shutil.copy2(sdk/'zlib/LICENSE',target/'zlib-NOTICE')
    shutil.copy2(cross/'root/usr/share/doc/gcc-12-aarch64-linux-gnu-base/copyright',target/'libstdc++-NOTICE')
print('Stream32 pinned sources and model/calibration hashes verified.')
