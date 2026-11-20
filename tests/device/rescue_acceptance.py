#!/usr/bin/env python3
"""Isolated, observed Falcon rescue flow. Never restarts the system host.

The controller accepts coordinates only after inspecting the fresh screenshot.
All real private/public trees are isolated by the existing locked harness. It
restores the initial full AMR in the fixture before the final development AMR.
"""
import argparse,hashlib,json,pathlib,re,shlex,subprocess,time,uuid,zipfile
from release_acceptance import ReleaseDevice,ROOT,APP
from release_lifecycle import install_termination_handler,defer_cleanup_signals

class RescueDevice(ReleaseDevice):
 def __init__(self,args):
  super().__init__(args.serial,args.package,['smoke'],True)
  self.discard_image_input=True
  self.test_package=args.fixture.resolve();self.original_package=args.original.resolve()
  self.out=ROOT/'.work/rescue/devices'/self.serial/str(uuid.uuid4());self.out.mkdir(parents=True)
  self.result.update(scope='rescue-only',fixture=str(self.test_package),original_package=str(self.original_package))
 def hashes(self,path):
  previous=None
  for attempt in range(40):
   rows=super().hashes(path)
   if all(re.fullmatch(r'[0-9a-f]{64}  .+',line) for line in rows):
    if rows==previous:return rows
    previous=rows
   else:previous=None
   time.sleep(.25)
  raise RuntimeError('Owned file tree did not settle for a complete SHA snapshot')
 def installed(self,package):
  self.push(package,self.remote_package)
  assert '"ret": 0' in self.sh('miniapp_cli install '+self.remote_package)
  matched=[]
  with zipfile.ZipFile(package) as archive:
   expected={name:hashlib.sha256(archive.read(name)).hexdigest() for name in ['manifest.json','app.js.bin','assets/bin/penchat-lvgl']}
  for slot in ['a','b']:
   path=self.data.rsplit('/',1)[0]+'/'+slot
   try:
    actual={name:self.sh('sha256sum '+shlex.quote(path+'/'+name)+' 2>/dev/null').split()[0] for name in expected}
   except IndexError:continue
   if actual==expected:matched.append(path)
  assert matched,'Installed package identity missing from owned Falcon A/B slots'
  self.result.setdefault('installed_packages',[]).append({'file':str(package),'sha256':hashlib.sha256(package.read_bytes()).hexdigest(),'unpacked_identity_verified':matched})
  self.record()
 def falcon_touch(self,values):
  assert not self.pid(),'Refuse Falcon rescue input while any native child is present'
  self.input_release()
  receipt=self.sh('PENCHAT_TOUCH_DEVICE='+self.event+' '+self.helper+' '+' '.join(map(str,values))+' 2>&1')
  assert not self.input_state()['btn_touch'] and not self.input_state()['active_slots'],receipt
 def launch(self):
  if self.key_helper_ready:self.wake()
  return super().launch()
 def rescue_state(self):
  try:return json.loads(self.sh('cat '+self.data+'/.rescue/state.json'))
  except (ValueError,subprocess.CalledProcessError):return {}
 def wake(self):
  path='/sys/class/backlight/'+('dcs-backlight' if self.serial=='7G12000010905426' else 'backlight')+'/bl_power'
  if self.sh('cat '+path)=='0':return
  key='/dev/input/by-path/'+('platform-502b000.i2c-event' if self.s6 else 'rk805-pwrkey')
  if self.s6:
   # Explicit CV1826 power device; discover its existing by-path alias by inode.
   key=self.sh('for p in /dev/input/by-path/*; do test "$(readlink -f "$p")" = /dev/input/event0 && echo "$p"; done').splitlines()[0]
  self.sh(self.key_helper+' '+shlex.quote(key)+' 116 1 100 300');time.sleep(.5)
  assert self.sh('cat '+path)=='0','Rescue screen did not wake'
 def observed(self,label):
  self.wake()
  path=self.system_capture(label)
  print(json.dumps({'event':'rescue-screen','serial':self.serial,'path':str(path),'native':self.pid(),'state':{k:v for k,v in self.rescue_state().items() if k in ('status','message','job','downloaded','total')}},ensure_ascii=False),flush=True)
 def smoke(self):
  # The outer wrapper has already proved ordinary startup with fake data.
  baseline=self.package
  (self.out/'commands.jsonl').touch()
  evidence={'ordinary_startup':True,'steps':[]};self.result['rescue_evidence']=evidence;self.record()
  self.sh('mkdir -p '+self.data+'/ime/empty '+self.data+'/assets '+self.public+'/导入/聊天')
  self.sh('printf rescue-fixture > '+self.data+'/assets/rescue-fixture.txt; printf public-fixture > '+self.public+'/导入/聊天/rescue-fixture.txt')
  self.close()
  try:
   self.installed(self.test_package)
   self.sh('miniapp_cli start '+APP['appid'])
   with zipfile.ZipFile(self.test_package) as archive:payload=json.loads(archive.read('assets/rescue/payload.json'))
   def matching():
    try:
     env=json.loads(self.sh('cat '+self.data+'/.rescue/environment.json'));return env if env.get('payload',{}).get('test_run')==payload['test_run'] else None
    except (ValueError,subprocess.CalledProcessError):return None
   env=self.wait(matching,30)
   assert env['payload']['test_run']==payload['test_run'] and not env['compatible'],env
   assert not self.pid(),'Wrong/faulty native program must not start'
   evidence['environment']=env;self.record();self.observed('failure')
   for line in iter(input,'done'):
    fields=line.strip().split();evidence['steps'].append(fields);self.record();self.wake()
    if not fields:continue
    if fields[0]=='tap':self.falcon_touch(self.raw(*map(int,fields[1:])));time.sleep(.4);self.observed('tap')
    elif fields[0]=='drag':
     x,y,ex,ey=map(int,fields[1:]);self.falcon_touch((*self.raw(x,y),*self.raw(ex,ey)));time.sleep(.4);self.observed('drag')
    elif fields[0]=='capture':self.observed('capture')
    elif fields[0]=='state':print(json.dumps({'event':'state','state':{k:v for k,v in self.rescue_state().items() if k in ('status','message','job','downloaded','total')}},ensure_ascii=False),flush=True)
    elif fields[0]=='wait':time.sleep(min(30,float(fields[1])));self.observed('wait')
    elif fields[0]=='verify':
     ticket=json.loads(self.sh('cat '+self.data+'/.rescue/install.json'));assert self.pid()
     assert self.sh('sha256sum /proc/'+self.pid()+'/exe').split()[0]==ticket['native_sha256']
     backup=ticket['backup']['path'];assert self.sh('cat '+backup+'/data/assets/rescue-fixture.txt')=='rescue-fixture'
     assert self.sh('cat '+backup+'/public/导入/聊天/rescue-fixture.txt')=='public-fixture'
     assert self.sh('test -d '+backup+'/data/ime/empty && echo yes')=='yes'
     evidence['verified_install_ticket']=ticket;evidence['backup_verified']=True;self.record();print('INSTALL_AND_BACKUP_VERIFIED',flush=True)
    else:raise ValueError('Unknown observed controller command')
   assert evidence.get('backup_verified'),'Actual package-manager install and backup still need validation'
   return evidence
  finally:
   with defer_cleanup_signals():
    # Ordinary close finishes native, whereas a failure page needs its observed
    # Return button before changing payloads. Controller should finish that page
    # before issuing done if installation was not successful.
    self.close();self.installed(self.original_package);self.package=self.original_package
    self.result['defer_original_resume']=True
    evidence['original_package_restored_in_fixture']=True;self.close()
    self.installed(baseline);self.package=baseline;self.launch();evidence['final_development_startup']=True;self.result['defer_original_resume']=False;self.record()

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--serial',required=True);p.add_argument('--package',required=True,type=pathlib.Path);p.add_argument('--fixture',required=True,type=pathlib.Path);p.add_argument('--original',required=True,type=pathlib.Path)
 a=p.parse_args();install_termination_handler();d=RescueDevice(a);print('ARTIFACTS '+str(d.out),flush=True);d.run()
if __name__=='__main__':main()
