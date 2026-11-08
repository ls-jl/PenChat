#!/usr/bin/env python3
"""Isolated real Falcon self-install. Never modifies the PenChat AppID/data."""
import argparse,hashlib,json,pathlib,shutil,subprocess,tempfile,time,uuid
ROOT=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);a=p.parse_args();assert a.serial in ('MEA2300009007113','MEB0400008900794')
appid='800'+str(uuid.uuid4().int%10**13).zfill(13);base=['adb','-s',a.serial]
def sh(cmd):return subprocess.check_output(base+['shell',cmd],text=True).replace('\r','').strip()
assert sh('test -d /userdisk/secondary/miniapp/data/mini_app/pkg/'+appid+' && echo exists')!='exists'
host=sh('pidof miniapp');data='/userdisk/secondary/miniapp/data/mini_app/pkg/'+appid+'/data';out=ROOT/'.work/update-selftest'/a.serial;out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='penchat-self-install-') as tmp:
 root=pathlib.Path(tmp);packages={}
 for version in ['0.0.2','0.0.1']:
  d=root/version;(d/'src').mkdir(parents=True);(d/'libs').mkdir()
  cfg=json.loads((ROOT/'package.json').read_text());cfg.update(name='PenChat isolated update',appid=appid,version=version,scripts={});(d/'package.json').write_text(json.dumps(cfg))
  (d/'src/app.js').write_text((ROOT/'src/app.js').read_text());shutil.copytree(ROOT/'src/platform',d/'src/platform');(d/'src/app.json').write_text('{"pages":{"index":"Page.vue"}}')
  shutil.copy(ROOT/cfg['icon'],d/cfg['icon']);shutil.copy(ROOT/'native/prebuilt/arm64-rk3562/libjsapi_penchat_launcher.so',d/'libs/libjsapi_penchat_launcher_selftest.so')
  if version=='0.0.1':
   digest=hashlib.sha256(packages['0.0.2'].read_bytes()).hexdigest()
   ticket=dict(appid=appid,version='0.0.2',operation=str(uuid.uuid4()),sha256=digest)
   action=r"""setTimeout(async()=>{try{const ticket=TICKET;ticket.path=String(globalThis.$dataDir).replace(/\/$/,'')+'/.updates/package.amr';const r=await launcher.install(ticket);let info=pm.getPackageInfo(ticket.appid);if(typeof info==='string')info=JSON.parse(info);if(r.res===0&&info.version==='0.0.2')launcher.reopen()}catch(e){this.message='INSTALL FAILED'}},2500)""".replace('TICKET',json.dumps(ticket))
  else:action="this.message='UPDATED 0.0.2'"
  # Test-only fixed geometry; production always uses Falcon cfg.json.
  (d/'src/Page.vue').write_text('<template><div class="page"><text class="title">{{message}}</text></div></template><script>\nimport pm from "pm"\nimport {launcher} from "penchatLauncher"\nexport default {data(){return {message:"UPDATE TEST '+version+'"}},mounted(){'+action+'}}\n</script><style>.page{width:930px;height:280px;background-color:#212121;}.title{color:#ffffff;font-size:26px;margin:40px;}</style>')
  with (out/(version+'-build.log')).open('w') as log:subprocess.run(['aiot-cli','-c','-q','-p'],cwd=d,stdout=log,stderr=subprocess.STDOUT,check=True)
  packages[version]=d/(appid+'.'+version.replace('.','_')+'.amr')
 try:
  subprocess.run(base+['push',str(packages['0.0.1']),'/tmp/penchat-isolated-update.amr'],check=True,capture_output=True)
  assert '"ret": 0' in sh('miniapp_cli install /tmp/penchat-isolated-update.amr')
  sh('mkdir -p '+data+'/.updates');subprocess.run(base+['push',str(packages['0.0.2']),data+'/.updates/package.amr'],check=True,capture_output=True)
  sh('miniapp_cli start '+appid)
  deadline=time.monotonic()+25;manifest={}
  while time.monotonic()<deadline:
   text=sh('cat /userdisk/secondary/miniapp/data/mini_app/pkg/'+appid+'/*/manifest.json 2>/dev/null || true')
   try:manifest=json.loads(text)
   except json.JSONDecodeError:pass
   if manifest.get('version')=='0.0.2':break
   time.sleep(.2)
  assert manifest.get('version')=='0.0.2','Real Falcon native install failed'
  time.sleep(1)
  # A fresh version-specific dynamic module path proves the new page was created.
  maps=sh('cat /proc/'+host+'/maps');assert '/'+appid+'/b/libs/' in maps or '/'+appid+'/a/libs/' in maps
  assert sh('pidof miniapp')==host
  result=dict(serial=a.serial,appid=appid,from_version='0.0.1',installed_version=manifest['version'],host_unchanged=True,host_pid=host)
  (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
 finally:
  sh('miniapp_cli start 8080222437664451');sh('miniapp_cli uninstall '+appid);sh('rm -f /tmp/penchat-isolated-update.amr')
