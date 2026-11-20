"""Pinned build overlay generation, source preservation and rejection checks."""
import hashlib,importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('overlay',ROOT/'scripts/generate-microtex-overlay.py');overlay=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlay)
class Overlay(unittest.TestCase):
  def test_verified_output_preserves_source(self):
    original=ROOT/'.deps/microtex';source=original/'src/atom/atom_matrix.cpp';before=source.read_bytes()
    with tempfile.TemporaryDirectory() as folder:
      out=pathlib.Path(folder);overlay.generate(original,out)
      row=json.loads((ROOT/'native/lvgl-app/microtex-overlays.json').read_text())['files'][0]
      self.assertEqual(hashlib.sha256((out/row['source']).read_bytes()).hexdigest(),row['output_sha256'])
      stamp=(out/row['source']).stat().st_mtime_ns;overlay.generate(original,out);self.assertEqual(stamp,(out/row['source']).stat().st_mtime_ns)
    self.assertEqual(source.read_bytes(),before)
  def test_unknown_source_is_rejected(self):
    with tempfile.TemporaryDirectory() as folder:
      path=pathlib.Path(folder);source=path/'source/src/atom';source.mkdir(parents=True);(source/'atom_matrix.cpp').write_text('unexpected source')
      with self.assertRaises(AssertionError):overlay.generate(path/'source',path/'output')
      self.assertFalse((path/'output').exists())
if __name__=='__main__':unittest.main()
