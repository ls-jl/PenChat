#!/usr/bin/env python3
"""Package an already built target in its own staging tree, without a release."""
import argparse,hashlib,json,pathlib,shutil,subprocess
from package_identity import amr_name
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('target',choices=['rk3562','cv1826-s']);args=p.parse_args()
if args.target=='cv1826-s':
    subprocess.run(['python3',str(ROOT/'scripts/package-cv1826-s-dev.py')],check=True)
    raise SystemExit(0)
app=json.loads((ROOT/'package.json').read_text());stage=ROOT/'build/package-rk3562'
shutil.rmtree(stage,ignore_errors=True);stage.mkdir(parents=True)
for name in ['package.json',app['icon']]:shutil.copy2(ROOT/name,stage/name)
shutil.copytree(ROOT/'src',stage/'src');shutil.copytree(ROOT/'assets',stage/'assets')
# Package current target builds, never a stale globally staged executable.
for name,build in [('penchat-lvgl','rk3562'),('penchat-sensor-guard','rk3562'),('penchat-scanner','scanner-rk3562')]:
    shutil.copy2(ROOT/'build'/build/name,stage/'assets/bin'/name)
subprocess.run(['bash',str(ROOT/'scripts/strip-production.sh'),str(stage/'assets/bin'),str(stage/'assets/scanner/lib')],check=True)
subprocess.run(['python3',str(ROOT/'scripts/stage-rescue.py'),args.target,str(stage)],check=True)
subprocess.run(['aiot-cli','-c','-q','-p'],cwd=stage,check=True)
package=stage/amr_name(app,args.target)
(stage/amr_name(app)).replace(package)
subprocess.run(['python3',str(ROOT/'tests/check_amr.py'),str(package)],check=True)
subprocess.run(['python3',str(ROOT/'tests/check_web_component.py'),str(package),args.target],check=True)
print(package)
