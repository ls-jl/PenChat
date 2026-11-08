#!/usr/bin/env python3
"""Package an already built target in its own staging tree, without a release."""
import argparse,hashlib,json,pathlib,shutil,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('target',choices=['orange','s6']);args=p.parse_args()
if args.target=='s6':
    subprocess.run(['python3',str(ROOT/'scripts/package-s6-dev.py')],check=True)
    raise SystemExit(0)
app=json.loads((ROOT/'package.json').read_text());stage=ROOT/'build/package-orange'
shutil.rmtree(stage,ignore_errors=True);stage.mkdir(parents=True)
for name in ['package.json',app['icon']]:shutil.copy2(ROOT/name,stage/name)
shutil.copytree(ROOT/'src',stage/'src');shutil.copytree(ROOT/'assets',stage/'assets')
launcher=stage/'libs/arm64-orange/libjsapi_penchat_launcher.so';launcher.parent.mkdir(parents=True)
shutil.copy2(ROOT/'native/prebuilt/arm64-orange/libjsapi_penchat_launcher.so',launcher)
subprocess.run(['bash',str(ROOT/'scripts/strip-production.sh'),str(stage/'assets/bin'),str(stage/'assets/scanner/lib'),str(launcher)],check=True)
loader=int(hashlib.sha256(launcher.read_bytes()).hexdigest()[:12],16)
shutil.copy2(launcher,stage/'libs'/('libjsapi_penchat_launcher_'+str(loader)+'.so'))
subprocess.run(['aiot-cli','-c','-q','-p'],cwd=stage,check=True)
package=stage/(app['appid']+'.'+app['version'].replace('.','_')+'.amr')
subprocess.run(['python3',str(ROOT/'tests/check_amr.py'),str(package)],check=True)
print(package)
