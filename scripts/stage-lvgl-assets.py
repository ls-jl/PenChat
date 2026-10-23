#!/usr/bin/env python3
import hashlib,json,pathlib,shutil,runpy
root=pathlib.Path(__file__).resolve().parents[1]
runpy.run_path(str(root/'scripts/stage-licenses.py'),run_name='__main__')
shutil.copytree(root/'.deps/microtex/res',root/'assets/microtex',dirs_exist_ok=True)
manifest={str(p.relative_to(root/'assets')):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'assets/microtex').rglob('*')) if p.is_file()}
(root/'native/lvgl-app/math-resources.sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
