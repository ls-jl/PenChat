#!/usr/bin/env python3
import hashlib,json,pathlib,shutil
root=pathlib.Path(__file__).resolve().parents[1]
licenses=root/'assets/licenses';licenses.mkdir(parents=True,exist_ok=True)
for name,path in {'miniz':'LICENSE','lvgl':'LICENCE.txt','md4c':'LICENSE.md','microtex':'LICENSE','tinyxml2':'LICENSE.txt','json':'LICENSE.MIT','cairo':'COPYING','pixman':'COPYING','freetype':'docs/FTL.TXT','webp':'COPYING'}.items():
    source=root/'.deps'/name/path
    if not source.exists():raise RuntimeError('Missing license '+str(source))
    shutil.copy2(source,licenses/(name+'.txt'))
shutil.copy2(root/'.deps/cairo/COPYING-LGPL-2.1',licenses/'cairo-LGPL-2.1.txt')
shutil.copy2(root/'.deps/cairo/COPYING-MPL-1.1',licenses/'cairo-MPL-1.1.txt')
shutil.copytree(root/'.deps/microtex/res',root/'assets/microtex',dirs_exist_ok=True)
manifest={str(p.relative_to(root/'assets')):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'assets/microtex').rglob('*')) if p.is_file()}
(root/'native/lvgl-app/math-resources.sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
shutil.copy2(root/'native/lvgl-app/dependencies.lock.json',licenses/'dependencies.lock.json')
