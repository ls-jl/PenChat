"""Offline CVI capability/ABI registry consistency; never opens device hardware."""
import hashlib,json,pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Compatibility(unittest.TestCase):
  def test_firmware_registry_matches_source_lock(self):
    data=json.loads((ROOT/'native/compositor/source-lock.json').read_text())
    source='#include "native/compositor/firmware.hpp"\n#include <iostream>\nint main(){for(const auto &p:penchat_compositor::firmwareProfiles())std::cout<<p.id<<"\\n"<<p.module<<"\\n"<<p.sha256<<"\\n"<<p.symbol<<"\\n"<<p.bits<<"\\n"<<p.machine<<"\\n";}\n'
    with tempfile.TemporaryDirectory(prefix='penchat-cvi-registry-',dir=ROOT/'.work') as directory:
      binary=pathlib.Path(directory)/'registry';subprocess.run(['g++','-std=c++14','-I',str(ROOT),'-x','c++','-','-o',str(binary)],input=source,text=True,check=True);actual=subprocess.check_output([str(binary)],text=True).splitlines()
    expected=[]
    for p in data['firmware_profiles']:expected += [p[k] for k in ['id','module','sha256','symbol']]+[str(p[k]) for k in ['bits','machine']]
    self.assertEqual(actual,expected)
    for relative,digest in data['adapters'].items():self.assertEqual(hashlib.sha256((ROOT/relative).read_bytes()).hexdigest(),digest,relative)
  def test_no_board_name_in_platform_default(self):
    source=(ROOT/'native/camera/src/device_profile.h').read_text();self.assertNotIn('Plum',source);self.assertNotIn('requiredBoard',source)
    launcher=(ROOT/'native/launcher/src/Launcher.cpp').read_text();self.assertNotIn('model.find("Plum")',launcher);self.assertIn('locateCviDisplay()',launcher)
  def test_sources_do_not_add_unknown_media_profile(self):
    source=(ROOT/'native/scanner/src/profile.hpp').read_text();self.assertIn('s6.opticalRegion={112,8,160,192}',source);self.assertIn('Cvitek Cv1826 for Youdao Plum V0 version',source)
if __name__=='__main__':unittest.main()
