#!/usr/bin/env python3
"""Generate verified MicroTeX source overlays without modifying the dependency tree."""
import hashlib,json,pathlib,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
def generate(source,output):
  source=source.resolve();output=output.resolve()
  assert source!=output and source not in output.parents,"Overlay output must not modify the dependency tree"
  lock=json.loads((ROOT/'native/lvgl-app/dependencies.lock.json').read_text())
  dependency=next(x for x in lock['dependencies'] if x['name']=='microtex')
  manifest=ROOT/'native/lvgl-app'/dependency['source_overlays']['manifest']
  digest=lambda b:hashlib.sha256(b).hexdigest()
  assert digest(manifest.read_bytes())==dependency['source_overlays']['sha256'],'Overlay manifest hash mismatch'
  spec=json.loads(manifest.read_text());assert spec['upstream_revision']==dependency['revision']
  for entry in spec['files']:
    path=pathlib.PurePosixPath(entry['source']);assert not path.is_absolute() and '..' not in path.parts
    data=(source/path).read_bytes();assert digest(data)==entry['source_sha256'],'MicroTeX source differs from pinned overlay'
    text=data.decode()
    for change in entry['replacements']:
      assert text.count(change['before'])==1,'Overlay context mismatch'
      text=text.replace(change['before'],change['after'])
    data=text.encode();assert digest(data)==entry['output_sha256'],'Overlay result hash mismatch'
    target=output/path;target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
if __name__=='__main__':
  assert len(sys.argv)==3
  generate(pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]))
