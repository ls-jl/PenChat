#!/usr/bin/env python3
"""Check the production AMR against staged, locked native resources."""
import hashlib, json, pathlib, re, struct, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
APP=json.loads((ROOT/'package.json').read_text())
path = ROOT / ('dist/'+APP['appid']+'.'+APP['version'].replace('.', '_')+'.amr')
with zipfile.ZipFile(path) as archive:
    names = archive.namelist()
    assert len(names) == len(set(names)), 'Duplicate package entries'
    manifest = json.loads(archive.read('manifest.json'))
    assert manifest['appName'] == APP['name']
    assert (manifest['appid'], manifest['version']) == (APP['appid'], APP['version'])
    icon = (ROOT / APP['icon']).read_bytes()
    assert APP['icon'] == 'icon-' + hashlib.sha256(icon).hexdigest()[:8] + '.png', 'Changed icon requires a new content-addressed filename'
    assert manifest['icon'] == APP['icon'] and archive.read(APP['icon']) == icon
    assert [n for n in names if n.endswith('.bin')] == ['app.js.bin']
    assert not any(re.search(r'(?i)(jsapi_(camera|openai)|openh264|features/|scanning/)', n) for n in names)
    assert [n for n in names if n.endswith('.rknn')] == ['assets/scanner/pintu_32.rknn']
    binary = archive.read('assets/bin/penchat-lvgl')
    assert binary[:5] == b'\x7fELF\x02' and struct.unpack_from('<H', binary, 18)[0] == 183
    assert binary == (ROOT / 'assets/bin/penchat-lvgl').read_bytes()
    assert sorted(n for n in names if n.startswith('assets/bin/') and not n.endswith('/')) == ['assets/bin/penchat-lvgl','assets/bin/penchat-scanner','assets/bin/penchat-sensor-guard']
    scanner = archive.read('assets/bin/penchat-scanner')
    assert scanner[:5] == b'\x7fELF\x02' and struct.unpack_from('<H',scanner,18)[0] == 183
    assert scanner == (ROOT/'assets/bin/penchat-scanner').read_bytes()
    assert b'test.press' not in scanner and b'scanner.test' not in binary
    scanlock=json.loads((ROOT/'native/scanner/dependencies.lock.json').read_text())
    assert archive.read('assets/scanner/dependencies.lock.json') == (ROOT/'native/scanner/dependencies.lock.json').read_bytes()
    expected_scan_files=set(scanlock['resources']) | {'NOTICE','dependencies.lock.json',
        'libpng-LICENSE','OpenCV-NOTICE','zlib-NOTICE','libstdc++-NOTICE'} | {'lib/'+name for name in scanlock['runtime']}
    assert {n[len('assets/scanner/'):] for n in names if n.startswith('assets/scanner/') and not n.endswith('/')}==expected_scan_files
    for name,entry in scanlock['resources'].items():
        assert hashlib.sha256(archive.read('assets/scanner/'+name)).hexdigest()==entry['sha256'],name
    for name,entry in scanlock['runtime'].items():
        assert hashlib.sha256(archive.read('assets/scanner/lib/'+name)).hexdigest()==entry['sha256'],name
    assert sorted(n.split('/')[-1] for n in names if n.startswith('assets/scanner/lib/') and not n.endswith('/'))==sorted(scanlock['runtime'])
    guard = archive.read('assets/bin/penchat-sensor-guard')
    assert guard[:5] == b'\x7fELF\x02' and struct.unpack_from('<H',guard,18)[0] == 183
    assert b'--test-report' not in guard
    bridges = [n for n in names if n.endswith('.so') and not n.startswith('assets/scanner/')]
    assert len(bridges) == 2 and all('libjsapi_penchat_launcher' in n for n in bridges)
    bridge = (ROOT / 'native/prebuilt/arm64-orange/libjsapi_penchat_launcher.so').read_bytes()
    assert all(archive.read(n) == bridge for n in bridges)
    for name, digest in json.loads((ROOT / 'native/lvgl-app/math-resources.sha256.json').read_text()).items():
        assert hashlib.sha256(archive.read('assets/' + name)).hexdigest() == digest, name
    assert archive.read('assets/licenses/dependencies.lock.json') == (ROOT / 'native/lvgl-app/dependencies.lock.json').read_bytes()
print(f'AMR: launcher-only JS, AArch64 native payload, bridges, dependency lock and all math resource hashes passed; {path.stat().st_size} bytes; SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()}')

# Reject former application identifiers anywhere in packaged content.
forbidden=[bytes.fromhex(s) for s in ['70656e677074','6c6974746c6570','6c6974746c652d70','e5b08f502b2b','38303031373739373939333630333633']]
with zipfile.ZipFile(path) as archive:
    for member in archive.namelist():
        data=archive.read(member).lower()
        assert not any(word.lower() in data or word.lower() in member.lower().encode() for word in forbidden), member
print('PenChat identity: no former identifiers in production payload.')

fontlock=json.loads((ROOT/'native/fonts/lock.json').read_text())
with zipfile.ZipFile(path) as archive:
    assert hashlib.sha256(archive.read('assets/fonts/'+fontlock['file'])).hexdigest()==fontlock['sha256']
    assert archive.read('assets/fonts/LICENSE.txt')==(ROOT/'native/fonts/LICENSE.txt').read_bytes()
print('Fallback font and redistribution license verified.')

# Dynamic symbols remain for the loader; private/debug symbols must not ship.
with zipfile.ZipFile(path) as archive:
    elf_count=0
    for member in archive.namelist():
        data=archive.read(member)
        if not data.startswith(b'\x7fELF'):continue
        elf_count+=1
        assert data[4:6]==b'\x02\x01',member
        offset=struct.unpack_from('<Q',data,40)[0]
        entry_size,count,names_index=struct.unpack_from('<HHH',data,58)
        assert entry_size==64 and 0<names_index<count and offset+count*entry_size<=len(data),member
        names_header=offset+names_index*entry_size
        names_offset,names_size=struct.unpack_from('<QQ',data,names_header+24)
        names=data[names_offset:names_offset+names_size]
        for i in range(count):
            name_offset=struct.unpack_from('<I',data,offset+i*entry_size)[0]
            name=names[name_offset:].split(b'\0',1)[0].decode('ascii')
            assert name not in ('.symtab','.gnu_debugdata') and not name.startswith(('.debug_','.zdebug_')), (member,name)
print(f'Production ELF stripping verified: {elf_count} files, no private symbol tables or debug sections.')
