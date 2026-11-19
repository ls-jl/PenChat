#!/usr/bin/env python3
"""Owned Falcon self-install fixture; only the locked release wrapper may run it."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import signal
import struct
import subprocess
import tempfile
import time
import uuid
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SERIALS = ('MEA2300009007113', 'MEB0400008900794', '7G12000010905426', 'JDA0700006552216')


def sha(payload):
  return hashlib.sha256(payload).hexdigest()


def require_parent_lock(serial):
  lock = ROOT/'.work/release-0.3.1/device-locks'/(serial+'.lock')
  assert os.environ.get('PENCHAT_RELEASE_OWNER_PID') == str(os.getppid())
  assert os.environ.get('PENCHAT_RELEASE_SERIAL') == serial
  assert os.environ.get('PENCHAT_RELEASE_LOCK_PATH') == str(lock)
  command = pathlib.Path('/proc/'+str(os.getppid())+'/cmdline').read_bytes()
  assert b'release_acceptance.py' in command and serial.encode() in command
  inode = str(lock.stat().st_ino)
  # --release-owned is not a lock bypass: the direct wrapper parent must own it.
  for line in pathlib.Path('/proc/locks').read_text().splitlines():
    fields = line.split()
    if (len(fields) >= 6 and fields[1] == 'FLOCK' and fields[3] == 'WRITE'
        and fields[4] == str(os.getppid()) and fields[5].rsplit(':', 1)[-1] == inode):
      return
  raise RuntimeError('Direct release wrapper parent does not own the serial flock')


def fixture_page(version, operation, action):
  # Exit is independent of every diagnostic promise, including a hung mkdir.
  return '''<template><div class="page"><text class="message">{{message}}</text></div></template>
<script>
import fs from 'fs'
import {launcher} from 'penchatLauncher'
const mark=async(event)=>{console.warn('PENCHAT_SELFTEST_OP_VERSION_'+event);try{await fs.mkdir('./selftest-OP/VERSION/'+event)}catch(error){/* independent console marker survives firmware fs limitations */}}
export default {
 data(){return {message:'UPDATE TEST VERSION',fixtureError:''}},
 async mounted(){
   const failed=(event,error)=>{this.fixtureError=event;this.message='FIXTURE ERROR '+event+(error?': '+String(error.message||error).slice(0,160):'')};
   this.finishTimer=setTimeout(()=>{
     try{mark('finish-requested').catch(error=>failed('finish-marker',error))}
     finally{this.$page.finish()}
   },15000);
   try{await mark('mounted')}catch(error){failed('mounted-marker',error);return}
   ACTION
 },
 onUnload(){clearTimeout(this.finishTimer);clearTimeout(this.installTimer);
   return mark('unloaded').catch(()=>{this.fixtureError='unload-marker';this.message='FIXTURE ERROR unload-marker'})},
 beforeDestroy(){clearTimeout(this.finishTimer);clearTimeout(this.installTimer)}
}
</script><style>.page{width:930px;height:280px;background-color:#212121;}.message{color:#ffffff;font-size:18px;}</style>
'''.replace('OP', operation).replace('VERSION', version).replace('ACTION', action)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--serial', required=True, choices=SERIALS)
  parser.add_argument('--release-owned', action='store_true', required=True)
  parser.add_argument('--package', required=True, type=pathlib.Path)
  args = parser.parse_args()
  require_parent_lock(args.serial)
  app = json.loads((ROOT/'package.json').read_text())
  package = args.package.resolve()
  with zipfile.ZipFile(package) as archive:
    assert len(archive.namelist()) == len(set(archive.namelist()))
    manifest = json.loads(archive.read('manifest.json'))
    assert (manifest['appid'], manifest['version']) == (app['appid'], app['version'])
    payload = json.loads(archive.read('assets/rescue/payload.json'))
    platform = 'cv1826-s' if args.serial == 'JDA0700006552216' else 'rk3562'
    bits, machine = (32, 40) if platform == 'cv1826-s' else (64, 183)
    assert payload['platform'] == platform and payload['abi_bits'] == bits
    member = payload['runtime']
    assert member.startswith('assets/runtime/') and '..' not in member
    launcher = archive.read(member)
    assert sha(launcher) == payload['runtime_sha256']
    assert launcher[:5] == b'\x7fELF'+bytes([bits//32]) and struct.unpack_from('<H', launcher, 18)[0] == machine
    libraries=[n for n in archive.namelist() if n.startswith('libs/') and n.endswith('.so')]
    assert len(libraries)==2 and all(
      n.startswith(('libs/arm/libjsapi_penchat_rescue_', 'libs/arm64/libjsapi_penchat_rescue_')) for n in libraries)
    assert {n.split('/')[1] for n in libraries}=={'arm','arm64'}
  operation = str(uuid.uuid4())
  appid = '800'+str(uuid.uuid4().int % 10**13).zfill(13)
  assert appid != app['appid']
  base = ['adb', '-s', args.serial]

  def sh(command):
    return subprocess.check_output(base+['shell', command], text=True, timeout=30).replace('\r', '').strip()

  def push(source, target):
    subprocess.run(base+['push', str(source), target], check=True, capture_output=True, timeout=60)

  own = '/userdisk/secondary/miniapp/data/mini_app/pkg/'+appid
  data = own+'/data'
  remote = '/tmp/penchat-self-install-'+operation+'.amr'
  markers = data+'/selftest-'+operation
  out = ROOT/'.work/update-selftest'/args.serial/operation
  out.mkdir(parents=True)
  record = dict(serial=args.serial, appid=appid, operation=operation, package=str(package),
                package_sha256=sha(package.read_bytes()), launcher_member=member,
                launcher_sha256=sha(launcher), owned_root=own, remote_package=remote,
                restored=False, status='prepared')

  def save():
    temporary = out/'recovery.json.tmp'
    with temporary.open('w') as file:
      json.dump(record, file, indent=2)
      file.flush()
      os.fsync(file.fileno())
    temporary.replace(out/'recovery.json')
    fd = os.open(out, os.O_DIRECTORY)
    try:
      os.fsync(fd)
    finally:
      os.close(fd)

  def exists(path):
    return sh('test -e '+path+' -o -L '+path+' && echo exists || true') == 'exists'

  def marker(version, event):
    if exists(markers+'/'+version+'/'+event):return True
    # Only this operation's explicit JS lifecycle markers are read from the
    # host's bounded log tail; never inspect another app's data or Storage.
    token='PENCHAT_SELFTEST_'+operation+'_'+version+'_'+event
    line=sh('tail -c 16777216 '+host_log+' | grep -F '+token+' | tail -1')
    return token in line

  assert sh('uname -m') == ('armv7l' if bits == 32 else 'aarch64')
  screen = json.loads(sh('cat /etc/miniapp/resources/cfg.json'))['screen']
  assert int(screen['width'])>0 and int(screen['height'])>0
  assert not exists(own), 'Random fixture identity already exists; refusing access'
  host = sh('pidof miniapp')
  assert host.isdigit(), 'Require one running MiniApp host; never start/restart it'
  host_log=sh('readlink /proc/'+host+'/fd/1')
  assert host_log.startswith('/userdata/applog/DictPen_') and host_log.endswith('.log')
  boot = sh('cat /proc/sys/kernel/random/boot_id')
  record.update(host_pid=host, boot_id=boot)
  save()
  mutated = False
  started = None

  def interrupted(signum, frame):
    raise InterruptedError('Signal '+str(signum)+'; retaining unknown fixture until own unload')

  signal.signal(signal.SIGTERM, interrupted)
  signal.signal(signal.SIGINT, interrupted)
  try:
    with tempfile.TemporaryDirectory(prefix='penchat-self-install-') as temporary:
      root = pathlib.Path(temporary)
      packages = {}
      for version in ('0.0.2', '0.0.1'):
        directory = root/version
        (directory/'src').mkdir(parents=True)
        (directory/'libs').mkdir()
        cfg = dict(app, name='PenChat isolated update', appid=appid, version=version, scripts={})
        (directory/'package.json').write_text(json.dumps(cfg))
        shutil.copy(ROOT/cfg['icon'], directory/cfg['icon'])
        # Only this random, disposable test AppID auto-loads the exact runtime
        # export to exercise its install method. Production packages continue
        # to auto-load only rescue and verify the runtime before lazy loading.
        libdir=directory/'libs'/('arm' if bits==32 else 'arm64');libdir.mkdir()
        (libdir/'libjsapi_penchat_launcher_selftest.so').write_bytes(launcher)
        shutil.copy(ROOT/'src/app.js', directory/'src/app.js')
        shutil.copytree(ROOT/'src/platform', directory/'src/platform')
        (directory/'src/app.json').write_text('{"pages":{"index":"Page.vue"}}')
        if version == '0.0.1':
          ticket = dict(appid=appid, version='0.0.2', operation=operation,
                        sha256=sha(packages['0.0.2'].read_bytes()))
          action = '''this.installTimer=setTimeout(async()=>{try{const ticket=TICKET;
            ticket.path=String(globalThis.$dataDir).replace(/\\/$/,'')+'/.updates/package.amr';
            const result=await launcher.install(ticket);
            if(result.res!==0)throw Error('install failed');
            launcher.reopen();}catch(error){this.message='INSTALL FAILED'}},2500);'''.replace('TICKET', json.dumps(ticket))
        else:
          action = "this.message='UPDATED 0.0.2';"
        # SDK fs.md documents mkdir (own data only), not writeFile. Marker directory
        # names encode operation/version/event; no Storage API or foreign path.
        source = fixture_page(version, operation, action).replace('width:930px;height:280px;', 'width:'+str(screen['width'])+'px;height:'+str(screen['height'])+'px;')
        (directory/'src/Page.vue').write_text(source)
        with (out/(version+'-build.log')).open('w') as log:
          subprocess.run(['aiot-cli', '-c', '-q', '-p'], cwd=directory, stdout=log, stderr=subprocess.STDOUT, check=True)
        packages[version] = directory/(appid+'.'+version.replace('.', '_')+'.amr')
      record['test_package_sha256'] = {v: sha(p.read_bytes()) for v, p in packages.items()}
      save()
      mutated = True  # Even failed push/install must pass through recovery.
      push(packages['0.0.1'], remote)
      assert '"ret": 0' in sh('miniapp_cli install '+remote)
      # Some deployed SDK builds return false for recursive mkdir despite the
      # documented API. Prepare only event-parent directories with adb; every
      # operation/version/event leaf must still be created by its real JS hook.
      sh('mkdir -p '+data+'/.updates '+markers+'/0.0.1 '+markers+'/0.0.2')
      for version in ('0.0.1','0.0.2'):
        for event in ('mounted','finish-requested','unloaded'):
          assert not exists(markers+'/'+version+'/'+event),'Lifecycle marker leaf must not be precreated'
      record.update(marker_parents_precreated=['0.0.1','0.0.2'],marker_leaves_absent_before_start=True)
      save()
      push(packages['0.0.2'], data+'/.updates/package.amr')
      started = time.monotonic()
      record['status'] = 'starting'
      save()
      sh('miniapp_cli start '+appid)
      deadline = started+45
      while time.monotonic() < deadline:
        if marker('0.0.2', 'mounted') and marker('0.0.2', 'unloaded'):
          break
        time.sleep(.25)
      assert marker('0.0.1', 'mounted'), 'Initial owned page never mounted'
      assert marker('0.0.1', 'unloaded'), 'Initial owned page did not unload on reopen'
      assert marker('0.0.2', 'mounted'), 'Updated page never mounted'
      assert marker('0.0.2', 'finish-requested'), 'Updated page did not request normal finish'
      assert marker('0.0.2', 'unloaded'), 'Updated page did not unload; fixture retained'
      receipt = json.loads(sh('cat '+data+'/.updates/install.json'))
      assert receipt['operation'] == operation and receipt['state'] == 'installed'
      record.update(status='assertions-passed', install_receipt=receipt)
  except BaseException as error:
    record.update(status='failed', error=str(error))
    save()
    raise
  finally:
    # Do not let repeated termination interrupt bounded ownership settlement.
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    if mutated:
      # Installation can have started before a failing start command returns.
      if started is not None:
        deadline = max(time.monotonic()+2, started+50)
        while time.monotonic() < deadline:
          mounted = [v for v in ('0.0.1', '0.0.2') if marker(v, 'mounted')]
          if marker('0.0.2', 'mounted') and marker('0.0.2', 'finish-requested') and all(marker(v, 'unloaded') for v in ('0.0.1', '0.0.2')):
            break
          time.sleep(.25)
      mounted = [v for v in ('0.0.1', '0.0.2') if marker(v, 'mounted')]
      settled = started is None or (marker('0.0.2', 'mounted') and marker('0.0.2', 'finish-requested')
                                    and all(marker(v, 'unloaded') for v in ('0.0.1', '0.0.2')))
      record['host_pid_after'] = sh('pidof miniapp')
      record['boot_id_after'] = sh('cat /proc/sys/kernel/random/boot_id')
      record['host_unchanged'] = record['host_pid_after'] == host and record['boot_id_after'] == boot
      if settled and record['host_unchanged']:
        record['uninstall_receipt'] = sh('miniapp_cli uninstall '+appid)
        record['owned_root_absent'] = not exists(own)
        sh('rm -f '+remote)
        record['remote_package_absent'] = not exists(remote)
        record['host_pid_after_cleanup'] = sh('pidof miniapp')
        record['boot_id_after_cleanup'] = sh('cat /proc/sys/kernel/random/boot_id')
        record['restored'] = (record['owned_root_absent'] and record['remote_package_absent']
                              and record['host_pid_after_cleanup'] == host
                              and record['boot_id_after_cleanup'] == boot)
      else:
        record['recovery_required'] = 'Unknown owned page or host state; no uninstall/host takeover attempted'
    else:
      record.update(restored=True, no_device_mutation=True)
    save()
    assert record['restored'], 'Owned fixture not safely cleaned; see '+str(out/'recovery.json')
  (out/'result.json').write_text(json.dumps(record, indent=2))
  print(json.dumps(record), flush=True)


if __name__ == '__main__':
  main()
