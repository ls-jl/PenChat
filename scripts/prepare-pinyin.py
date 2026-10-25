#!/usr/bin/env python3
"""Apply the checksum-pinned PenChat overlay to a separate generated source tree."""
import hashlib,json,pathlib,shutil,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
lock=json.loads((ROOT/'native/pinyin/source-lock.json').read_text())
source=ROOT/'.deps/libpinyinime';target=ROOT/'.deps/libpinyinime-penchat'
patch=ROOT/'native/pinyin/penchat.patch'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(patch)==lock['patch_sha256'],'Pinyin adapter patch checksum differs'
for name,entry in lock['files'].items():assert sha(source/name)==entry['source_sha256'],name
if not target.exists() or any(not (target/n).is_file() or sha(target/n)!=e['adapted_sha256'] for n,e in lock['files'].items()):
 if target.exists():shutil.rmtree(target)
 shutil.copytree(source,target)
 file=target/'src/pinyin_ime.cpp'
 file.write_text(file.read_text().rstrip('\n')+'\n')
 subprocess.run(['patch','-s','-p1','-i',str(patch)],cwd=target,check=True)
for name,entry in lock['files'].items():assert sha(target/name)==entry['adapted_sha256'],name
print('libpinyinime: source, adapter and dictionary hashes verified.')

assets=ROOT/'assets/pinyin'
assets.mkdir(parents=True,exist_ok=True)
for name in ['pinyin.txt','dictionary.data']:
 shutil.copyfile(source/name,assets/name)
