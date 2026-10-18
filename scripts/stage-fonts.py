#!/usr/bin/env python3
import pathlib,json,hashlib,shutil
root=pathlib.Path(__file__).resolve().parents[1]
source=root/'native/fonts'; lock=json.loads((source/'lock.json').read_text())
font=source/lock['file']
assert hashlib.sha256(font.read_bytes()).hexdigest()==lock['sha256']
(root/'assets/fonts').mkdir(parents=True,exist_ok=True)
for name in [lock['file'],'LICENSE.txt','lock.json']:shutil.copy2(source/name,root/'assets/fonts'/name)
