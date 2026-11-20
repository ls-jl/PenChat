"""Pinned build overlay generation, source preservation and rejection checks."""
import hashlib,importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('overlay',ROOT/'scripts/generate-microtex-overlay.py');overlay=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlay)
class Overlay(unittest.TestCase):
  def test_verified_output_preserves_source(self):
    original=ROOT/'.deps/microtex';rows=json.loads((ROOT/'native/lvgl-app/microtex-overlays.json').read_text())['files'];before={r['source']:(original/r['source']).read_bytes() for r in rows}
    with tempfile.TemporaryDirectory() as folder:
      out=pathlib.Path(folder);overlay.generate(original,out)
      stamps={}
      for row in rows:
        path=out/row['source'];self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),row['output_sha256']);stamps[row['source']]=path.stat().st_mtime_ns
      overlay.generate(original,out)
      for path,stamp in stamps.items():self.assertEqual(stamp,(out/path).stat().st_mtime_ns)
    for path,content in before.items():self.assertEqual((original/path).read_bytes(),content)
  def test_dependency_tree_cannot_be_output(self):
    original=ROOT/'.deps/microtex'
    for output in [original,original/'generated']:
      with self.assertRaises(AssertionError):overlay.generate(original,output)
  def test_unknown_source_is_rejected(self):
    with tempfile.TemporaryDirectory() as folder:
      path=pathlib.Path(folder);source=path/'source/src/atom';source.mkdir(parents=True);(source/'atom_matrix.cpp').write_text('unexpected source')
      with self.assertRaises(AssertionError):overlay.generate(path/'source',path/'output')
      self.assertFalse((path/'output').exists())
if __name__=='__main__':unittest.main()
