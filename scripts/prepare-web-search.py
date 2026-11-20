#!/usr/bin/env python3
"""Verify originals/overlays and publish an immutable, shared source cache."""
import fcntl,hashlib,json,os,pathlib,shutil,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
source=ROOT/'native/web-search/upstream';lock=json.loads((source.parent/'source-lock.json').read_text())
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
for name,expected in lock['sha256'].items():
    if digest(source/name)!=expected:raise ValueError('Edge source changed: '+name)
for name,expected in lock.get('integration_sha256',{}).items():
    if digest(source.parent/name)!=expected:raise ValueError('Unreviewed Edge overlay: '+name)
target=ROOT/'.deps/edge-search';target.parent.mkdir(exist_ok=True)
fingerprint=hashlib.sha256(json.dumps(lock,sort_keys=True).encode()).hexdigest()
with (target.parent/'edge-search.prepare.lock').open('a') as guard:
    fcntl.flock(guard,fcntl.LOCK_EX)
    marker=target/'.penchat-source-cache.json'
    try:cached=json.loads(marker.read_text())
    except (OSError,ValueError):cached={}
    valid=cached.get('fingerprint')==fingerprint and all((target/name).is_file() and digest(target/name)==expected for name,expected in cached.get('files',{}).items())
    if not valid:
        with tempfile.TemporaryDirectory(prefix='edge-source-',dir=target.parent) as temporary:
            stage=pathlib.Path(temporary)/'source';shutil.copytree(source,stage)
            for patch in sorted((source.parent/'patches').glob('*.patch')):
                subprocess.run(['patch','--batch','-p1','-i',str(patch.resolve())],cwd=stage,check=True,stdout=subprocess.DEVNULL)
            files={str(p.relative_to(stage)):digest(p) for p in stage.rglob('*') if p.is_file()}
            same=target.is_dir() and all((target/name).is_file() and digest(target/name)==value for name,value in files.items())
            if not same:
                if target.exists():shutil.rmtree(target)
                stage.rename(target)
            marker.write_text(json.dumps({'fingerprint':fingerprint,'files':files},sort_keys=True)+'\n')
print(target)
