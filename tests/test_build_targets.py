#!/usr/bin/env python3
"""SoC build identity selects the correct compiler, ABI and package suffix offline."""
import importlib.util,json,pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package_identity',ROOT/'scripts/package_identity.py')
identity=importlib.util.module_from_spec(spec);spec.loader.exec_module(identity)
class BuildTargets(unittest.TestCase):
    def test_platform_ocr_capability(self):
        with tempfile.TemporaryDirectory(prefix='penchat-device-capability-') as folder:
            for cvi in (False,True):
                binary=str(pathlib.Path(folder)/('cvi' if cvi else 'rk'))
                source='#include "device_profile.h"\nint main(){return camera::deviceCapabilities().multiLineOcr=='+('false' if cvi else 'true')+'?0:1;}\n'
                command=['g++','-std=c++14','-x','c++','-I',str(ROOT/'native/camera/src'),'-','-o',binary]
                if cvi:command.append('-DPENCHAT_CVI_PLATFORM=1')
                subprocess.run(command,input=source,text=True,check=True)
                subprocess.run([binary],check=True)
    def test_target_abi_and_compiler(self):
        for name,cpu,machine,libdir,triple in [('rk3562','aarch64','183','arm64-rk3562','aarch64-linux-gnu'),('cv1826-s','arm','40','arm32-cv1826-s','arm-linux-gnueabihf')]:
            with self.subTest(target=name):
                command='source scripts/native-target.sh "$1"; printf "%s\\n" "$PENCHAT_BUILD_CPU" "$PENCHAT_BUILD_ELF_MACHINE" "$PENCHAT_BUILD_LIBDIR"; "$PENCHAT_BUILD_CC" -dumpmachine'
                lines=subprocess.check_output(['bash','-e','-c',command,'build-target-test',name],cwd=ROOT,text=True).splitlines()
                self.assertEqual(lines,[cpu,machine,libdir,triple])
    def test_unknown_build_names_rejected(self):
        for name in ['orange','s6','cv1826','unknown']:
            result=subprocess.run(['bash','-c','source scripts/native-target.sh "$1"','build-target-test',name],cwd=ROOT,capture_output=True)
            self.assertNotEqual(result.returncode,0)
    def test_artifact_identity(self):
        app=json.loads((ROOT/'package.json').read_text())
        base=app['appid']+'.'+app['version'].replace('.','_')
        self.assertEqual(identity.amr_name(app),base+'.amr')
        for target in identity.TARGETS:self.assertEqual(identity.amr_name(app,target),base+'-'+target+'.amr')
        with self.assertRaises(ValueError):identity.amr_name(app,'s6')
if __name__=='__main__':unittest.main()
