"""Real package validation and CVI updater transport, without device data."""
import hashlib,json,pathlib,subprocess,tempfile,unittest
from test_update import APPID,BINARY,Node,Probe,package
ROOT=pathlib.Path(__file__).resolve().parents[1]
def elf(platform):
 b=bytearray(64);b[:6]=b'\x7fELF'+bytes([1 if platform=='cv1826-s' else 2,1]);b[18:20]=(40 if platform=='cv1826-s' else 183).to_bytes(2,'little');b[37]=4 if platform=='cv1826-s' else 0;return bytes(b)
class Platforms(unittest.TestCase):
 def validate(self,body,target,success):
  with tempfile.TemporaryDirectory() as folder:
   p=pathlib.Path(folder)/'p.amr';p.write_bytes(body)
   result=subprocess.run([str(BINARY),'--validate',str(p),APPID,'0.3.6',hashlib.sha256(body).hexdigest(),target],capture_output=True,text=True)
   self.assertEqual(result.returncode==0,success,result.stdout+result.stderr)
   return json.loads(result.stdout)
 def test_both_targets(self):
  for target in ('rk3562','cv1826-s'):self.validate(package('0.3.6',platform=target),target,True)
 def test_wrong_target_main(self):
  for target,other in [('rk3562','cv1826-s'),('cv1826-s','rk3562')]:self.validate(package('0.3.6',platform=other),target,False)
 def test_wrong_support_library(self):
  self.validate(package('0.3.6',extra={'assets/runtime/injected.so':elf('cv1826-s')}),'rk3562',False)
 def test_exact_rescue_exception(self):
  self.validate(package('0.3.6',extra={'libs/arm/libjsapi_penchat_rescue_'+('a'*16)+'.so':elf('cv1826-s')}),'rk3562',True)
  self.validate(package('0.3.6',extra={'libs/arm/not-rescue.so':elf('cv1826-s')}),'rk3562',False)
 def test_rescue_directory_must_match_elf(self):
  self.validate(package('0.3.6',extra={'libs/arm/libjsapi_penchat_rescue_'+('a'*16)+'.so':elf('rk3562')}),'rk3562',False)
 def test_descriptor_must_match(self):
  for d in [{'platform':'cv1826-s','abi_bits':32},{'platform':'rk3562','abi_bits':32},None]:
   self.validate(package('0.3.6',extra={'assets/rescue/payload.json':json.dumps(d)}),'rk3562',False)
 def test_native_hash_from_same_crc_pass(self):
  r=self.validate(package('0.3.6',platform='cv1826-s'),'cv1826-s',True);self.assertEqual(r['native_sha256'],hashlib.sha256(elf('cv1826-s')).hexdigest())
 def test_cvi_source_download_ticket_and_recovery(self):
  node=Node();probe=None
  try:
   body=package('0.3.6',platform='cv1826-s');node.release('0.3.6',body)
   node.files={p.replace('/penchat-rk/','/penchat-cvi-s/'):b for p,b in node.files.items()}
   with tempfile.TemporaryDirectory() as folder:
    root='http://127.0.0.1:'+str(node.port)+'/penchat-cvi-s/'
    probe=Probe(folder,root,nodes=['127.0.0.1'],current='0.3.5',platform='cv1826-s');probe.check();probe.cmd('download',snapshot={'draft':'retained','session':'fixture'})
    s=probe.wait(lambda s:s['status'] in ('ready','error'));self.assertEqual(s['status'],'ready',s)
    self.assertEqual((probe.path/'.updates/package.amr').read_bytes(),body);probe.cmd('install_requested');probe.wait(lambda s:s['status']=='installing');probe.close();probe=None
    probe=Probe(folder,root,nodes=['127.0.0.1'],current='0.3.5',platform='cv1826-s');self.assertEqual(probe.cmd('state')['status'],'uncertain')
    self.assertEqual(json.loads((probe.path/'.updates/restore.json').read_text())['snapshot']['draft'],'retained')
    self.assertFalse(any(p.startswith('/penchat-rk/') for _,p,*_ in node.requests))
  finally:
   if probe:probe.close()
   node.close()
if __name__=='__main__':unittest.main()
