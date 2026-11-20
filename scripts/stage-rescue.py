#!/usr/bin/env python3
"""Stage only ABI-selected rescue bridges; the normal runtime is lazy-loaded."""
import argparse,hashlib,json,pathlib,shutil,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('target',choices=['rk3562','cv1826-s']);p.add_argument('stage',type=pathlib.Path);a=p.parse_args()
stage=a.stage.resolve()
def stripped(source,target):
    strip=subprocess.check_output(['bash','-c','source scripts/native-target.sh "$1"; printf "%s" "$PENCHAT_BUILD_STRIP"','stage',target],cwd=ROOT,text=True)
    shutil.copy2(source,stage/'rescue-stripping.tmp')
    subprocess.run([strip,'--strip-unneeded',str(stage/'rescue-stripping.tmp')],check=True)
    data=(stage/'rescue-stripping.tmp').read_bytes();(stage/'rescue-stripping.tmp').unlink();return data
shutil.rmtree(stage/'libs',ignore_errors=True)
for target,abi in [('rk3562','arm64'),('cv1826-s','arm')]:
    subprocess.run(['bash',str(ROOT/'scripts/build-rescue.sh'),target],check=True)
    data=stripped(ROOT/'build'/target/'rescue/libjsapi_penchat_rescue.so',target)
    folder=stage/'libs'/abi;folder.mkdir(parents=True,exist_ok=True)
    name='libjsapi_penchat_rescue_'+hashlib.sha256(data).hexdigest()[:16]+'.so';(folder/name).write_bytes(data)
subprocess.run(['cmake','--build',str(ROOT/'build'/a.target),'--target','jsapi_penchat_launcher','-j4'],check=True)
runtime=stripped(ROOT/'build'/a.target/'libjsapi_penchat_launcher.so',a.target)
digest=hashlib.sha256(runtime).hexdigest();relative='assets/runtime/libpenchat_runtime_'+digest[:16]+'.so'
shutil.rmtree(stage/'assets/runtime',ignore_errors=True);(stage/'assets/runtime').mkdir(parents=True)
(stage/relative).write_bytes(runtime)
(stage/'assets/rescue').mkdir(parents=True,exist_ok=True)
(stage/'assets/rescue/payload.json').write_text(json.dumps({'format':1,'platform':a.target,'abi_bits':64 if a.target=='rk3562' else 32,'runtime':relative,'runtime_sha256':digest},indent=2)+'\n')
print('Rescue ABI pair and lazy runtime staged:',a.target)
