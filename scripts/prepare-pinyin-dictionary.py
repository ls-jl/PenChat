#!/usr/bin/env python3
"""Build read-only weighted resources from the locked Apache-2.0 Rime source."""
import collections, hashlib, json, pathlib, urllib.request, sys, itertools
ROOT = pathlib.Path(__file__).resolve().parents[1]
lock_path = ROOT/'native/pinyin/dictionary-lock.json'
lock = json.loads(lock_path.read_text())
source = ROOT/'.deps/pinyin-dictionary/pinyin_simp.dict.yaml'
if not source.exists():
    source.parent.mkdir(parents=True, exist_ok=True)
    data = urllib.request.urlopen(lock['url'], timeout=60).read()
    assert hashlib.sha256(data).hexdigest() == lock['sha256']
    source.write_bytes(data)
assert hashlib.sha256(source.read_bytes()).hexdigest() == lock['sha256']
words = collections.defaultdict(dict)
syllables = collections.defaultdict(set)
for line in source.read_text().splitlines():
    row = line.split('\t')
    if len(row) != 3:
        continue
    word, spelling, weight = row
    parts = spelling.split()
    assert len(parts) == len(word) and parts and len(parts) <= 32
    assert all(p.isascii() and p.isalpha() and p.islower() and len(p) <= 6 for p in parts)
    # Both spellings are used by existing private profiles and keyboards.
    # Copy weights unchanged; this is a phonetic alias, not extra frequency.
    aliases={'lue':('lue','lve'),'nue':('nue','nve')}
    assert sum(p in aliases for p in parts)<=4
    for variant in itertools.product(*(aliases.get(p,(p,)) for p in parts)):
        key = "'".join(variant)
        words[key][word] = max(words[key].get(word, 0), int(weight))
        for p, c in zip(variant, word):
            syllables[p].add(c)
table = ''.join(p+','+''.join(sorted(chars))+'\n' for p, chars in sorted(syllables.items()))
dictionary = ''.join(p+' '+str(len(entries))+' '+
                     ' '.join(w+' '+str(n) for w,n in sorted(entries.items()))+'\n'
                     for p,entries in sorted(words.items()))
target = ROOT/'assets/pinyin'; target.mkdir(parents=True, exist_ok=True)
resources = {'pinyin.txt': table.encode(), 'dictionary.data': dictionary.encode()}
assert sum(map(len, resources.values())) <= 5*1024*1024
for name, data in resources.items():
    digest = hashlib.sha256(data).hexdigest()
    if '--update-lock' in sys.argv:
        lock['resources'][name] = {'sha256': digest, 'bytes': len(data)}
    assert lock['resources'][name]['sha256'] == digest, (name, digest)
    path=target/name
    if not path.exists() or path.read_bytes()!=data:
        pending=path.with_suffix(path.suffix+'.part')
        pending.write_bytes(data);pending.replace(path)
if '--update-lock' in sys.argv:
    lock_path.write_text(json.dumps(lock, indent=2)+'\n')
print(f'Weighted pinyin: {sum(map(len, words.values()))} entries; {sum(map(len,resources.values()))} bytes; source/output hashes verified.')
