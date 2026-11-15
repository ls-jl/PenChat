"""Host-only full-disk recovery checkpoint contracts; never touches a device."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from release_records import write_json,require_space
class Records(unittest.TestCase):
 def test_checkpoint_survives_sync_failure(self):
  with tempfile.TemporaryDirectory() as folder:
   path=Path(folder)/'recovery.json';write_json(path,{'original':'saved'})
   with mock.patch('release_records.os.fsync',side_effect=OSError(28,'No space')):
    with self.assertRaises(OSError):write_json(path,{'original':'lost'})
   self.assertEqual(json.loads(path.read_text()),{'original':'saved'})
   self.assertFalse(path.with_name(path.name+'.pending').exists())
 def test_insufficient_space_stops_before_scope(self):
  with mock.patch('release_records.shutil.disk_usage',return_value=mock.Mock(free=0)):
   with self.assertRaisesRegex(RuntimeError,'not started'):require_space('.')
if __name__=='__main__':unittest.main()
