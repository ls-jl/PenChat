#!/usr/bin/env python3
"""SoC build identity selects the correct compiler, ABI and package suffix offline."""
import importlib.util,json,pathlib,subprocess,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package_identity',ROOT/'scripts/package_identity.py')
identity=importlib.util.module_from_spec(spec);spec.loader.exec_module(identity)
class BuildTargets(unittest.TestCase):
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
