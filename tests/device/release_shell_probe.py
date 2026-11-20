"""Isolated A2/B2 shell experiment with live markers and baseline shell restore."""
import hashlib
import json
import pathlib
import tempfile
import time
import zipfile

from release_lifecycle import _context, free_back_start


BASELINE = ('candidate-4-focus-diagnostic-rk3562.amr', 'd44d0f652fd8bab0c6ab90e9694f8cc8384ff94260ada28516bb54828c17b8c6')
PROBES = [
  ('A2', '8e422dd2b2871daaa83aeed93019df59e768614458b6adac60ce1388eeaeff38', 1000000),
  ('B2', '13eee550217fdc2acca893dc54170b7645ca26731239ab27efe435ec7386f901', 2000000),
]


def focus_sequence(s):
  entries = s['input_focus_trace']['entries']
  return max((entry['focus_sequence'] for entry in entries), default=0)


def cleanup(d):
  """Verify baseline in fake data; use only its observed failure-page Return if cached."""
  assert d.serial == '7G12000010905426'
  assert hashlib.sha256(d.package.read_bytes()).hexdigest() == BASELINE[1]
  d.result['defer_original_resume'] = True
  s = d.state();initial = focus_sequence(s)
  used_return = False
  if initial >= 1000000:
    d.close()
    time.sleep(.5)
    image=d.system_capture('own-launcher-failure-before-return')
    print(json.dumps({'event':'controller_own_failure_page_ready',
      'image':str(image)},ensure_ascii=False),flush=True)
    command = input().strip().split()
    assert len(command) == 3 and command[0] == 'own-return', 'Only an observed own-page Return action is accepted'
    x,y = map(int,command[1:])
    assert not d.pid(), 'Do not act on fallback controls while native is running'
    d.touch(x,y)
    time.sleep(.5)
    d.launch()
    used_return = True
  s = d.state();seq = focus_sequence(s)
  cleared = 0 < seq < 1000000
  d.result.update(restore_package_hash=BASELINE[1],shell_marker_cleared=cleared,
    restored_shell_focus_sequence=seq,defer_original_resume=not cleared)
  assert cleared, 'Baseline shell remains unconfirmed; real data will not be started under probe shell'
  return {'restore_package_hash':BASELINE[1],'shell_marker_cleared':True,
    'initial_sequence':initial,'restored_sequence':seq,'own_failure_return_used':used_return,
    'verification_data':'isolated fake fixture only'}


def run_b2(d):
  """B2 follows the already confirmed ineffective A2; cleanup closes its own page."""
  assert d.serial == '7G12000010905426'
  assert hashlib.sha256(d.package.read_bytes()).hexdigest() == PROBES[1][1]
  root = pathlib.Path(__file__).resolve().parents[2]
  baseline = root/'.work/release-0.3.1/packages'/BASELINE[0]
  d.result.update(shell_marker_cleared=False,defer_original_resume=True)

  def observed_return(label):
    if not d.pid():
      return {'native_already_stopped':True}
    d.close();time.sleep(.5);image=d.system_capture(label)
    print(json.dumps({'event':'controller_own_failure_page_ready','image':str(image)},ensure_ascii=False),flush=True)
    command=input().strip().split()
    assert len(command)==3 and command[0]=='own-return'
    assert not d.pid()
    d.touch(*map(int,command[1:]));time.sleep(.6)
    return {'own_failure_return':list(map(int,command[1:])),'image':str(image)}

  result = {'variant':'B2','package_sha256':PROBES[1][1]}
  try:
    before=d.chat();seq=focus_sequence(before)
    assert 2000000<=seq<3000000, 'B2 live marker missing; no effectiveness judgement permitted'
    result.update(marker_confirmed=True,focus_sequence=seq)
    # The unchanged launcher has a 15s ready timeout. Keeping its native alive
    # beyond that bound also rules out probing an unready, eventless hole gate.
    native=d.pid();time.sleep(16)
    before=d.state();assert d.pid()==native and before['visible'] and before['input_focused']
    x,y=free_back_start(before,d.w,d.h);points=(*d.raw(x,y),*d.raw(x+max(80,d.w/4),y))
    d.input_release();before=d.state()
    receipt=d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+' '.join(map(str,points))+' 2>&1')
    time.sleep(.7);after=d.state() if d.pid() else None
    result.update(start=[x,y],receipt=receipt,effective_root_back=after is None,
      before={k:before.get(k) for k in ('pid','diagnostic_time','visible','page_visible','window_focused','input_focused','input_focus_trace')},
      after=None if after is None else {k:after.get(k) for k in ('pid','visible','page_visible','window_focused','input_focused','touch','back_navigation','input_focus_trace')})
    (d.out/'hole-touch-B2-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
  finally:
    try:
      close=observed_return('B2-own-failure-before-return')
      assert hashlib.sha256(baseline.read_bytes()).hexdigest()==BASELINE[1]
      d.push(baseline,d.remote_package);assert '"ret": 0' in d.sh('miniapp_cli install '+d.remote_package)
      d.package=baseline;d.launch();restored=d.state();seq=focus_sequence(restored)
      cleared=0<seq<1000000
      if not cleared:
        observed_return('baseline-own-failure-before-return');d.launch();seq=focus_sequence(d.state());cleared=0<seq<1000000
      d.result.update(restore_package_hash=BASELINE[1],shell_marker_cleared=cleared,
        restored_shell_focus_sequence=seq,defer_original_resume=not cleared,probe_page_close=close)
      assert cleared,'Baseline live shell marker still not cleared'
      d.close()
    except BaseException:
      d.result['defer_original_resume']=True;d.close()
      d.push(baseline,d.remote_package);assert '"ret": 0' in d.sh('miniapp_cli install '+d.remote_package)
      d.result['restore_package_hash']=BASELINE[1]
      raise
  result.update(restore_package_hash=BASELINE[1],shell_marker_cleared=True,
    production_compliance='pending compatibility checks if effective; not approved')
  return result


def run(d):
  assert d.serial == '7G12000010905426'
  results = []
  root = pathlib.Path(__file__).resolve().parents[2]
  baseline = root/'.work/release-0.3.1/packages'/BASELINE[0]
  assert hashlib.sha256(baseline.read_bytes()).hexdigest() == BASELINE[1]
  d.result.update(probe_variants=results, shell_marker_cleared=False)

  def install(package, digest):
    assert hashlib.sha256(package.read_bytes()).hexdigest() == digest
    with zipfile.ZipFile(package) as archive:
      assert hashlib.sha256(archive.read('assets/bin/penchat-lvgl')).hexdigest() == 'f82d8f47a24c583a787ae414b73261bea613e8e6d02113d0107d9269501f83ed'
    d.push(package, d.remote_package)
    assert '"ret": 0' in d.sh('miniapp_cli install '+d.remote_package)
    d.package = package
    d.launch()

  def unload_page():
    if not d.pid():
      return {'native_already_stopped': True}
    s = d.wait(lambda: (s if (s := d.state())['visible'] and s['page_visible'] and
      s['input_focused'] and s['touch_input']['active'] else None), 10)
    context = _context(d.serial, d.sh)
    capability = json.loads(d.sh(context['home']+' --discover'))
    assert capability.get('supported'), 'No uniquely verified system Home for clean probe page unloading'
    assert d.sh(context['home']+' --send '+capability['path']) == 'verified-home-paired'
    d.wait(lambda: not d.pid(), 15)
    time.sleep(.3)
    return {'verified_home': capability, 'native_stopped_after_home': True}

  def reset_fake():
    d.close()
    d.sh('rm -rf '+d.data+' '+d.public)
    d.sh('mkdir -p '+d.data+' '+d.public)
    with tempfile.TemporaryDirectory(prefix='penchat-shell-probe-') as temp:
      db = pathlib.Path(temp)/'penchat.db';d.fixture(db);d.push(db, d.data+'/penchat.db')

  try:
    for name, digest, marker in PROBES:
      package = pathlib.Path(__file__).resolve().parents[2]/('.work/y07-hole-touch-probe/'+name+'/8001792332371649.0_3_1.amr')
      if name != 'A2':
        unloaded = unload_page()
        reset_fake()
        install(package, digest)
      else:
        unloaded = {'initial_scope_install': True}
      before = d.chat()
      seq = focus_sequence(before)
      if not marker < seq < marker+1000000:
        # A verified Home closes the old page before one controlled reload.
        unloaded = unload_page();d.launch();before = d.state();seq = focus_sequence(before)
      item = {'variant': name, 'package_sha256': digest, 'marker_expected': marker,
              'focus_sequence': seq, 'marker_confirmed': marker < seq < marker+1000000,
              'page_unload': unloaded}
      values = [e['focus_sequence'] for e in before['input_focus_trace']['entries'] if e['focus_sequence']]
      item['focus_sequence_range'] = [min(values), max(values)] if values else [0,0]
      results.append(item)
      (d.out/('hole-touch-'+name+'-marker.json')).write_text(json.dumps(item,ensure_ascii=False,indent=2)+'\n')
      d.record()
      assert item['marker_confirmed'], 'Installed probe shell marker is absent; do not infer an ineffective handler'
      x, y = free_back_start(before, d.w, d.h)
      d.input_release();before = d.state()
      points = (*d.raw(x,y), *d.raw(min(d.w-1,x+max(80,d.w/4)),y))
      receipt = d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+' '.join(map(str,points))+' 2>&1')
      time.sleep(.7)
      after = d.state() if d.pid() else None
      item.update(start=[x,y], receipt=receipt, native_exit_observed=after is None,
        before={k:before.get(k) for k in ('pid','visible','page_visible','window_focused','input_focused','back_navigation','input_focus_trace')},
        after=None if after is None else {k:after.get(k) for k in ('pid','visible','page_visible','window_focused','input_focused','back_navigation','touch','input_focus_trace')})
      item['effective_root_back'] = after is None
      (d.out/('hole-touch-'+name+'-result.json')).write_text(json.dumps(item,ensure_ascii=False,indent=2)+'\n')
      if item['effective_root_back']:
        break
  finally:
    try:
      unload = unload_page() if d.pid() else {'native_already_stopped': True}
      d.close()
      install(baseline, BASELINE[1])
      restored = d.state();seq = focus_sequence(restored)
      if seq >= 1000000:
        unload = unload_page();d.launch();restored = d.state();seq = focus_sequence(restored)
      cleared = 0 < seq < 1000000
      d.result.update(restore_package_hash=BASELINE[1], shell_marker_cleared=cleared,
        restored_shell_focus_sequence=seq, probe_cleanup_unload=unload)
      if not cleared:
        d.result['defer_original_resume'] = True
      assert cleared, 'Baseline shell marker not cleared; keep native stopped instead of exposing experimental shell to user data'
      d.close()
    except BaseException:
      d.result['defer_original_resume'] = True
      d.close()
      d.push(baseline, d.remote_package)
      assert '"ret": 0' in d.sh('miniapp_cli install '+d.remote_package)
      d.package = baseline
      d.result['restore_package_hash'] = BASELINE[1]
      raise
  result = {'variants': results, 'restore_package_hash': BASELINE[1],
    'shell_marker_cleared': d.result.get('shell_marker_cleared'),
    'production_compliance': 'not approved; effective variant still requires child/cancel/top-panel/official/lifecycle checks'}
  (d.out/'hole-touch-probe-summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
  return result
