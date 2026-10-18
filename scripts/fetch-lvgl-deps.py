#!/usr/bin/env python3
"""Fetch source archives into ignored .deps; verify every locked checksum."""
import concurrent.futures, hashlib, json, pathlib, tarfile, urllib.request
ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = ROOT / 'native/lvgl-app/dependencies.lock.json'
def fetch(item):
    root = ROOT / '.deps' / item['name']
    archive = ROOT / '.deps/downloads' / (item['name'] + '.tar')
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        with urllib.request.urlopen(item['url'], timeout=120) as response:
            content = response.read()
        archive.write_bytes(content)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if item.get('sha256') and digest != item['sha256']:
        raise RuntimeError('source checksum mismatch: ' + item['name'])
    item['sha256'] = digest
    if not (root / '.source-sha256').exists():
        root.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            members = tar.getmembers()
            prefix = members[0].name.split('/')[0] + '/'
            for member in members:
                if member.name == prefix[:-1]: continue
                if not member.name.startswith(prefix): raise RuntimeError('unexpected archive path')
                member.name = member.name[len(prefix):]
                if '..' in pathlib.PurePosixPath(member.name).parts or member.name.startswith('/'):
                    raise RuntimeError('unsafe archive path')
                tar.extract(member, root, filter='data')
        (root / '.source-sha256').write_text(digest)
    elif (root / '.source-sha256').read_text() != digest:
        raise RuntimeError('extracted source mismatch: ' + item['name'])
    print(item['name'], digest, flush=True)
    return item
if __name__ == '__main__':
    lock = json.loads(LOCK.read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        lock['dependencies'] = list(pool.map(fetch, lock['dependencies']))
    # Only the explicit bootstrap flag may populate the initial lock.
    import sys
    if '--lock' in sys.argv:
        LOCK.write_text(json.dumps(lock, indent=2) + '\n')
