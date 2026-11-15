#!/usr/bin/env python3
"""Run explicit isolated release modules on one explicitly authorized device."""
import argparse
import json
import pathlib
import shutil
import signal
import subprocess
import sys
import time

from release_acceptance import APP, DEVICES, FEATURES, ROOT, defer_cleanup_signals


def supervise(command, log, recovery_timeout=180):
  """Forward one stop to our child and bound the recovery wait without killing it."""
  request = []
  previous = {number: signal.getsignal(number) for number in (signal.SIGTERM, signal.SIGINT)}
  for number in previous:
    signal.signal(number, lambda signum, frame: request.append(signum))
  child = None
  forwarded = False
  deadline = None
  try:
    child = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    while child.poll() is None:
      if request and not forwarded:
        child.send_signal(signal.SIGTERM)
        forwarded = True
        deadline = time.monotonic()+recovery_timeout
      if deadline is not None and time.monotonic() >= deadline:
        return {'returncode': None, 'interrupted': True, 'recovery_timeout': True,
          'child_pid': child.pid, 'stop_forwarded': forwarded}
      time.sleep(.1)
    return {'returncode': child.returncode, 'interrupted': bool(request),
      'recovery_timeout': False, 'child_pid': child.pid, 'stop_forwarded': forwarded}
  finally:
    for number, handler in previous.items():
      signal.signal(number, handler)


def _main(stop_requests):
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--serial', required=True, choices=tuple(DEVICES))
  parser.add_argument('--package', required=True, type=pathlib.Path)
  parser.add_argument('--candidate', required=True)
  parser.add_argument('--features', required=True, nargs='+', choices=tuple(FEATURES))
  parser.add_argument('--continue-functional-failures', action='store_true')
  args = parser.parse_args()
  out = ROOT/'.work/release-0.3.1/matrix'/args.candidate/args.serial
  out.mkdir(parents=True, exist_ok=True)
  source = ROOT/'.work/release-0.3.1/devices'/args.serial
  blocker = out/'supervisor-recovery.json'
  if blocker.exists():
    raise RuntimeError('Previous supervised child requires explicit recovery: '+str(blocker))
  manifest = {'serial': args.serial, 'device': DEVICES[args.serial], 'candidate': args.candidate,
    'package': str(args.package.resolve()), 'version': APP['version'], 'modules': [],
    'policy': 'Saved private/public data protected; user explicitly permits discarding unsubmitted input; no unrelated app starts or host restart',
    'visual_approval': 'pending except already-approved white solid keycap color'}

  def record():
    (out/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')

  for feature in args.features:
    if stop_requests:
      manifest['stopped'] = 'Termination requested between scopes; no further scope'
      break
    folder = out/feature
    folder.mkdir(exist_ok=True)
    entry = {'feature': feature, 'status': 'running', 'artifacts': str(folder)}
    manifest['modules'].append(entry)
    record()
    command = [sys.executable, str(ROOT/'tests/device/release_acceptance.py'),
      '--serial', args.serial, '--package', str(args.package.resolve()),
      '--discard-pending-input', '--features', feature]
    started = time.time()
    with (folder/'run.log').open('w') as log:
      supervision = supervise(command, log)
    status = supervision['returncode']
    entry['supervision'] = supervision
    if supervision['recovery_timeout']:
      entry.update(status='recovery-required', returncode=None,
        error='Owned runner recovery timed out; not killed; no further scope allowed')
      manifest['stopped'] = entry['error']
      blocker.write_text(json.dumps(supervision, indent=2)+'\n')
      record()
      break
    result_path = source/'result.json'
    result = None
    if result_path.exists() and result_path.stat().st_mtime >= started:
      result = json.loads(result_path.read_text())
    for path in source.iterdir():
      if path.is_file() and path.stat().st_mtime >= started:
        shutil.copy2(path, folder/path.name)
    if (source/feature).is_dir():
      shutil.copytree(source/feature, folder/'child', dirs_exist_ok=True)
    entry['returncode'] = status
    entry['elapsed_seconds'] = time.time()-started
    if result:
      entry['restoration'] = {key: result.get(key) for key in
        ('restored', 'host_unchanged', 'original_app_resumed', 'recovery_errors')}
      entry['lifecycle_closes'] = result.get('lifecycle_closes', [])
      if status == 0 and result.get('functional_selection_completed'):
        entry['status'] = 'assertions-passed-review-pending'
      else:
        entry['status'] = 'needs-review'
        entry['error'] = result.get('error', 'Harness did not complete')
      if not result.get('restored') or not result.get('host_unchanged'):
        entry['status'] = 'recovery-required'
        manifest['stopped'] = 'Original data/host restoration requires attention; no further device action'
        record()
        break
      if result.get('foreground_failures'):
        entry['status'] = 'interrupted-by-lifecycle'
        manifest['stopped'] = 'Foreground lost; device no longer exclusively available for testing'
        record()
        break
    else:
      entry['status'] = 'preflight-deferred'
      entry['error'] = (folder/'run.log').read_text()[-3000:]
      manifest['stopped'] = 'No fresh completed isolation result; stop rather than infer recovery'
      record()
      break
    record()
    if supervision['interrupted'] or stop_requests:
      manifest['stopped'] = 'Termination requested; child exit and recovery evidence collected; no further scope'
      break
    if status and not args.continue_functional_failures:
      manifest['stopped'] = 'Functional/harness failure requires review'
      break
  manifest['selected_modules_finished'] = len(manifest['modules']) == len(args.features) and 'stopped' not in manifest
  record()
  print(json.dumps(manifest, ensure_ascii=False))


def main():
  # Keep repeated stop requests harmless through final manifest publication too.
  with defer_cleanup_signals() as stop_requests:
    return _main(stop_requests)


if __name__ == '__main__':
  main()
