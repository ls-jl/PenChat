#!/usr/bin/env python3
"""Release hardware runner with complete outer private/public data isolation.

Only the explicitly authorized Y01, Y02, Y07 and S6 are permitted.
`--plan` performs no adb operations. Execution requires an explicit package and
feature selection. Existing feature harnesses run only inside the outer fixture;
their legacy assumptions can therefore fail without exposing the user's DB,
assets, migration backups, IME profile or public import/export directory.

Functional assertions and screenshots are separate evidence: no image produced
by this runner counts as human visual approval or physical paper acceptance.
"""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
import uuid
import zlib

from refresh_bottom_acceptance import APP, ROOT, Device as ScopedDevice
from release_records import write_json,require_space

DEVICES = {
  'MEB0400008900794': 'Y01',
  'MEA2300009007113': 'Y02',
  '7G12000010905426': 'Y07',
  'JDA0700006552216': 'S6',
}
RELEASE = 'release-'+APP['version']
OUT = ROOT/'.work'/RELEASE/'devices'
def scoped_output(path):
  value=pathlib.Path(path).resolve();base=(ROOT/'.work').resolve()
  assert value!=base and base in value.parents,'Acceptance output must stay in a task subdirectory of .work'
  return value
# Reuse individual feature fixtures, never acceptance.py on original user data.
FEATURES = {
  'backup-restore': 'Isolated incremental backup restore, conflicts, cancel, invalid files and native reload',
  'resource-residency': 'Control center retains underlay/worker/model and observes but gates pen input',
  'scan-original': 'Real scan result original PNG, reopen cache and no persistence/send',
  'scan-confirm-latency': 'S6 paper original/reopen plus explicit isolated save timing and exact stored file hash; never sends',
  'scan-original-codec': 'Synthetic saved scan original on device: failed decode retry, long PNG and zoom; no optical claim',
  'feedback-preview': 'CVI metadata-only feedback preview without opening ISP nodes',
  'keyboard-handoff': 'Direct system keyboard, normal return, default preference and completed composer visibility',
  'panel-snapshot': 'System panel background handoff and own-page restore',
  'official-editing-dark': 'Y07 retained official editing with opaque dark handoff backing',
  'official-editing': 'Y07 retained IME cursor, selection, copy/cut/paste, local single send and light boundary pixels',
  'scanner-cycles': 'Twenty profile-verified real capture/discard, resident worker and guardian return cycles; no optical claim',
  'smoke': 'Native identity, Falcon geometry, reader/drawer and all 27 Settings rows',
  'keyboard-review': 'Light keyboard, composer growth and drawer before/after screenshots',
  'global-font-preedit': '75/100/150 global UI fonts and Wide/Portrait floating preedit with real candidate touches',
  'font-persistence': 'Save text150 with unchanged UIscale, native restart verify, then reset100 and restart verify',
  'preedit-light-review': 'Approved white keycaps with real Chinese preedit/candidate geometry in light mode',
  'y07-back-trace': 'Two-second native-only raw syscall trace around one isolated back gesture',
  'preset-camera-large-font': 'Real preset apply plus 150% preset/toast/camera captions and capture cancellation',
  'y07-back-free-ring': 'One free-edge back with bounded numeric focus/panel transitions',
  'y07-back-inner-ring': 'One edge-minus-one back in the same title band with numeric focus transitions',
  'y07-back-visual': 'One held back gesture with exactly one Falcon and one native frame for preview identification',
  'y07-back-x34-ring': 'One x34 outside-native-edge focus comparison in the same unoccupied title band',
  'y07-hole-probe-ab': 'Marker-verified isolated A2/B2 shell back probe, followed by baseline shell reinstallation',
  'y07-shell-cleanup': 'Verify unmodified baseline shell in fake data before resuming real user data',
  'y07-hole-probe-b2': 'Confirm B2 two-million live marker, probe one back, then observed own-page close and baseline restore',
  'y07-native-back-validation': 'New inner-edge child/root Back, short cancel, focus cancel and picker/key/read priorities',
  'native-back-validation': 'Actual inner-edge child/root Back, short cancel and picker/key/read ownership delta',
  'real-background-probe': 'Marker-verified one-shot own-app background, same-PID editor/reading resume and plain-shell cleanup',
  'real-background-generation': 'Actual one-shot onHide retains and progresses local fake stream, resumes same PID and never resends',
  'real-background-scanner': 'Real onHide releases prepared capture/key/guardian, retains healthy worker/model and rearms after resume',
  'y07-scroll-surface': 'Light/dark whole-reader scrolling with bounded native/SDK captures; physical flash judgement separate',
  'owned-official-readback': 'Owned official controls observed through read-only dynamically selected MiniApp primary',
  'owned-official-drm': 'Three read-only DRM ownership boundaries around owned official keyboard and one edit',
  'owned-official-default': 'Saved default official routing with retained native field, owned cancel/reopen/confirm and preference reset',
  'y07-scroll-dark': 'Dark-only whole-reader scroll completion delta',
  'owned-official-ime': 'Explicit owned official editor session, SDK screenshot and observed cancel preserving fake input',
  'settings-controls': 'Real toggle/choice/numeric changes, nondefault persistence through native restart and About dialog',
  'feedback-diagnostics': 'Bounded key sampling, real 30s timeout/release and whitelisted preview; never uploads',
  'update-check': 'Read-only fixed-source update check and return; never installs an update',
  'update-self-install': 'Random nonexistent owned test AppID self-installs exact-release launcher, proves both page unloads and uninstalls',
  'update-self-install-diagnostic': 'One owned fixture run with fresh SDK frames before/after its independent finish watchdog; never an installation pass by pictures',
  'auxiliary-title': 'Fake local one-shot title after first answer, no second-turn/restart overwrite and old manual title retention',
  'auxiliary-compression': 'Fake local automatic/manual Responses compression, full archive, opaque exclusion and checkpoint restart',
  'y07-render-cost': 'Reader drags, keyboard open/hide and drawer open/close with boundary-only LVGL render counters',
  'dpi': '75/100/125/150% scale, theme persistence and rich-text screenshots',
  'refresh-bottom': '30/60 Hz timing, persistence, direction reversal and latest-bottom stability',
  'onboarding': 'Fresh install seven onboarding animations and completion',
  'api-compatibility': 'Local fake Chat/Responses JSON, image, usage, HTTP error and manual retry',
  'anthropic': 'Local fake Messages SSE, private replay, usage, compression and retry',
  'stream-reading': 'Local slow SSE, independent reading, live actions and return to bottom',
  'chat-whitespace': 'Blank draft, normalization and isolated local requests',
  'message-edit': 'Edit/resend, context replacement and archive confirmation',
  'reply-actions': 'Retry alternatives, archive and branch actions',
  'session-info': 'Conversation statistics and local usage accounting',
  'archive-reader': 'Archived content, reading, images and restore',
  'transfer-format': 'Three format readers, exact ZIP resources and sample imports',
  'model-catalog': 'Capability fills, explicit manual parameters and save',
  'font-size': 'Saved reading font size and layout',
  'pull-sensitivity': 'Saved pull setting, cancel and trigger boundaries',
  'pinyin': 'Chinese26/English/Chinese9, candidates, correction and private learning',
  'pinyin-core': 'Native layouts/candidates/correction/learning/restart cycles; official/background explicitly remain pending',
  'native-touch': 'Real type-B frames, rollover, cancellation and lifecycle',
  'native-touch-core': '400 real type-B alternating-slot keys, edge keys and normal root finish; retained background separate',
  'composer-touch': 'Composer attachment/text target boundaries',
  'back-navigation': 'Child/root edge navigation, editor and viewer retention',
  'history-time': 'History time modes and missing timestamp behavior',
  'official-keyboard': 'Official IME handoff/return; human content inspection still required',
  'photo-gestures': 'Image multitouch and camera crop lifecycle (legacy landscape fixture)',
  'camera-orientation': 'Y07 camera poses and true HD saved dimensions',
  'scanner-profile': 'Explicit sensor frames, scanner/OCR/background ownership and format restoration',
  'scanner-profile-core': '20 real frame capture/discard/rearm cycles, idle model retention, crash format handback and OCR; retained background separate',
}


def render_window(before, after, hz):
  """Compare monotonic LVGL counters without treating idle intervals as jank."""
  assert hz in (30, 60), 'Render budget requires the configured 30/60 Hz policy'
  frames = after['frames']-before['frames']
  total = after['render_total_us']-before['render_total_us']
  buckets = [a-b for a, b in zip(after['render_buckets'], before['render_buckets'])]
  assert len(buckets) == 6 and frames > 0 and total >= 0
  assert min(buckets) >= 0 and sum(buckets) == frames, 'Counters reset or histogram inconsistent'
  limit = 16667 if hz == 60 else 33334
  budget_bucket = 1 if hz == 60 else 2
  over = sum(buckets[budget_bucket+1:])
  result = {
    'native_rendered_frames': frames,
    'render_total_us': total,
    'average_render_cost_us': total/frames,
    'render_bucket_limits_us': [8000, 16667, 33334, 50000, 100000, None],
    'render_buckets': buckets,
    'configured_ui_rate_hz': hz,
    'render_budget_threshold_us': limit,
    'renders_over_budget': over,
    'renders_over_budget_percent': 100*over/frames,
    'diagnostic_sample_span_us': after['sample_time_us']-before['sample_time_us'],
    'interval_total_us_delta_including_idle': after['interval_total_us']-before['interval_total_us'],
    'cumulative_render_max_before_us': before['render_max_us'],
    'cumulative_render_max_after_us': after['render_max_us'],
    'cumulative_interval_max_before_us': before['interval_max_us'],
    'cumulative_interval_max_after_us': after['interval_max_us'],
    'limitations': [
      'These are LVGL render costs, not hardware refresh or display presentation measurements.',
      'Display render cost includes its synchronous flush/present work; present cost is a diagnostic subset and must not be added to render total.',
      'Render intervals include intentional idle gaps and cannot establish FPS or jank.',
      'Cumulative maximum fields do not provide the exact maximum within this window.',
      'Only boundary diagnostics are taken; no screenshot or diagnostic is requested during drags.',
    ],
  }
  if all(k in before and k in after for k in ('present_calls', 'presented', 'present_total_us', 'present_max_us')):
    calls = after['present_calls']-before['present_calls']
    accepted = after['presented']-before['presented']
    present_total = after['present_total_us']-before['present_total_us']
    assert calls >= 0 and 0 <= accepted <= calls and present_total >= 0
    result['surface_present_timing'] = {
      'present_calls': calls, 'accepted_calls': accepted, 'present_total_us': present_total,
      'average_present_call_cost_us': present_total/calls if calls else None,
      'cumulative_present_max_before_us': before['present_max_us'],
      'cumulative_present_max_after_us': after['present_max_us'],
      'limitation': 'Accepted Surface.present calls are not measured physical display refreshes',
    }
  return result


def snapshot_window(before, after):
  """Report synchronous snapshot preparation separately from display renders."""
  count = after['count']-before['count']
  total = after['total_us']-before['total_us']
  assert count >= 0 and total >= 0, 'Surface snapshot counters reset'
  assert count or total == 0, 'Snapshot elapsed time changed without a snapshot'
  return {
    'snapshot_count': count,
    'snapshot_total_us': total,
    'average_snapshot_cost_us': total/count if count else None,
    'cumulative_snapshot_max_before_us': before['max_us'],
    'cumulative_snapshot_max_after_us': after['max_us'],
    'scope': 'Synchronous SurfaceSnapshot allocation and lv_snapshot_take drawing',
    'limitation': 'Cumulative maxima cannot establish this window maximum; snapshot work is separate from LVGL display render cost',
  }


@contextlib.contextmanager
def defer_cleanup_signals():
  """Finish recovery even when a supervisor repeats its stop request."""
  deferred = []
  previous = {number: signal.getsignal(number) for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP)}
  for number in previous:
    signal.signal(number, lambda signum, frame: deferred.append(signum))
  try:
    yield deferred
  finally:
    for number, handler in previous.items():
      signal.signal(number, handler)


def stop_owned_child(child, timeout=45):
  """Request nested recovery once; never kill or abandon it as completed."""
  if child.poll() is None:
    child.send_signal(signal.SIGTERM)
  try:
    child.wait(timeout=timeout)
  except subprocess.TimeoutExpired:
    return False
  return True


class ReleaseDevice(ScopedDevice):
  def __init__(self, serial, package, features, discard_input=False, startup_rollback_guard=False):
    if serial not in DEVICES:
      raise ValueError('Release scope permits only explicit Y01/Y02/Y07 serials')
    super().__init__(serial, package)
    self.name = DEVICES[serial]
    self.features = features
    if 'backup-restore' in features:
      assert features == ['backup-restore'], 'Backup restoration needs its own isolated scope'
    if 'scan-original-codec' in features:
      assert features == ['scan-original-codec'], 'The saved-scan codec fixture needs its own isolated scope'
    self.discard_input = discard_input
    self.startup_rollback_guard = startup_rollback_guard
    self.out = OUT/serial
    self.out.mkdir(parents=True, exist_ok=True)
    self.saved = self.data+'.'+RELEASE+'-saved'
    self.public_saved = self.public+'.'+RELEASE+'-saved'
    self.helper = '/tmp/penchat-release-tap'
    self.remote_package = '/tmp/penchat-release.amr'
    self.snapshot = '/tmp/penchat-release-snapshot.db'
    self.snapshot_helper = '/tmp/penchat-release-snapshot'
    self.input_probe = '/tmp/penchat-release-input-state'
    self.input_state_probe = self.input_probe
    self.input_probe_ready = False
    self.key_helper = '/tmp/penchat-release-key'
    self.key_helper_ready = False
    self.testing_payload_installed = False
    self.official_readback_probe = features in (['owned-official-readback'],['official-editing'],['official-editing-dark']) or (features == ['keyboard-handoff'] and serial != 'JDA0700006552216')
    self.official_default_probe = features in (['owned-official-default'],['owned-official-drm'],['owned-official-readback'],['official-editing'],['official-editing-dark'])
    self.official_confirm_probe = features in (['owned-official-ime'], ['owned-official-default'], ['owned-official-readback'], ['official-editing'],['official-editing-dark'])
    self.official_fake_text='penchatimefixture-'+uuid.uuid4().hex[:8]
    self.result.update(device=self.name, release_scope=list(DEVICES),
                       human_visual_approval='pending', paper_scan_quality='pending',
                       keyboard_brightness_approval='approved by user: white solid keycaps',
                       checks=[], screenshots=[], input_trace=[])

  def sh(self, command):
    # Every adb invocation, including those inherited from ScopedDevice, is
    # guarded. No global adb shell, implicit serial or disallowed device exists.
    assert self.serial in DEVICES
    assert all(appid == APP['appid'] for appid in re.findall(r'\bminiapp_cli\s+start\s+(\d+)', command)), 'Never launch an unrelated app from acceptance'
    return super().sh(command)

  def close(self):
    initial_new_back_cleanup = 'y07-native-back-validation' in self.features and not self.testing_payload_installed
    if initial_new_back_cleanup or any(feature in ('y07-back-free-ring','y07-back-inner-ring','y07-back-visual','y07-back-x34-ring','y07-hole-probe-ab','y07-shell-cleanup','y07-hole-probe-b2') for feature in self.features):
      # Keep the controlled diagnostic to exactly its requested gesture. Its
      # cleanup is explicitly a native-only TERM, never a lifecycle pass.
      native = self.pid()
      if native:
        assert APP['appid'] in self.sh("tr '\\0' ' ' < /proc/"+native+'/cmdline')
        self.sh('kill -TERM '+native)
        self.wait(lambda: not self.pid(), 30)
      self.result.setdefault('lifecycle_closes', []).append({'normal_finish': False,
        'forced_term': bool(native), 'reason': 'Controlled focus diagnostic cleanup; no extra back contacts'})
      return
    from release_lifecycle import close_app
    result = close_app(self.serial, APP['appid'], self.sh, self.pid, self.state)
    self.result.setdefault('lifecycle_closes', []).append(result)

  def capture(self, name):
    super().capture(name)
    self.result['screenshots'].append({'path': str(self.out/(name+'.png')),
                                       'visual_review': 'pending'})

  def system_capture(self, name):
    # SDK capture redraws the scene; it is not passive framebuffer readback.
    # Never invoke it on a retired page or a different application's foreground.
    owner=self.state() if self.pid() else None
    from release_official_readback import owned_fullscreen
    if not owner or not (owner.get('page_visible') or
        owned_fullscreen(owner,getattr(self,'owned_official_request',None))):
      raise RuntimeError('SDK screenshot requires a live owned page/editor')
    token=uuid.uuid4().hex
    remote='/tmp/penchat-release-system-'+token+'.png'
    local=self.out/(name+'-'+token+'.png');partial=local.with_suffix('.partial')
    metadata={'remote':remote,'path':str(local),'host_start_ns':time.time_ns(),
      'host_monotonic_start_ns':time.monotonic_ns()}
    def boundary(key):
      try:
        metadata[key]=self.state() if self.pid() else None
      except Exception as error:
        metadata[key+'_error']=str(error)
    def complete_png(path):
      data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n'
      offset=8;first=True;compressed=[]
      while offset<len(data):
        assert offset+12<=len(data)
        length=struct.unpack_from('>I',data,offset)[0];kind=data[offset+4:offset+8]
        end=offset+12+length;assert end<=len(data)
        payload=data[offset+8:offset+8+length]
        assert zlib.crc32(kind+payload)&0xffffffff==struct.unpack_from('>I',data,end-4)[0]
        if first:
          assert kind==b'IHDR' and length==13
          metadata['png_width'],metadata['png_height']=struct.unpack_from('>II',payload)
          assert 0<metadata['png_width']<=8192 and 0<metadata['png_height']<=8192
          first=False
        if kind==b'IDAT':compressed.append(payload)
        offset=end
        if kind==b'IEND':
          assert length==0 and offset==len(data) and compressed
          maximum=(metadata['png_width']*8+1)*metadata['png_height']
          decoder=zlib.decompressobj();decoded=decoder.decompress(b''.join(compressed),maximum+1)
          assert decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail and 0<len(decoded)<=maximum
          return hashlib.sha256(data).hexdigest()
      raise AssertionError('PNG has no complete IEND')
    boundary('native_before')
    try:
      assert self.sh('test ! -e '+remote+' && echo new')=='new'
      metadata['cli_started_ns']=time.time_ns()
      metadata['cli_receipt']=self.sh('miniapp_cli capture '+remote)
      metadata['cli_returned_ns']=time.time_ns()
      deadline=time.monotonic()+10;last='New PNG is not yet available'
      while time.monotonic()<deadline:
        if self.sh('test -s '+remote+' && echo ready || true')=='ready':
          try:
            subprocess.run(['adb','-s',self.serial,'pull',remote,str(partial)],check=True,capture_output=True)
            metadata['png_sha256']=complete_png(partial)
            partial.replace(local);break
          except (AssertionError,OSError,struct.error,zlib.error,subprocess.CalledProcessError) as error:
            last=str(error)
        time.sleep(.15)
      else:raise AssertionError('Fresh complete Falcon PNG unavailable: '+last)
      metadata['complete_verified_ns']=time.time_ns()
      metadata['validation']='Fresh UUID path; PNG signature, IHDR, all chunk CRCs, complete zlib stream, terminal IEND'
      self.result['screenshots'].append({'path':str(local),'visual_review':'pending','surface':'Falcon',
        'capture_metadata':str(local.with_suffix('.capture.json'))})
      return local
    except BaseException as error:
      metadata['error']=str(error);raise
    finally:
      boundary('native_after')
      metadata['host_end_ns']=time.time_ns();metadata['host_monotonic_end_ns']=time.monotonic_ns()
      local.with_suffix('.capture.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n')
      self.sh('rm -f '+remote)

  def record(self):
    write_json(self.out/'result.json',self.result)

  def input_state(self, release=False):
    assert self.input_probe_ready
    value = json.loads(self.sh(self.input_probe+' '+shlex.quote(self.event)+(' --release' if release else '')))
    assert 'error' not in value, 'Touch probe failed: '+json.dumps(value)
    if release:
      assert not value['btn_touch'] and not value['active_slots'], 'Touch release did not reach the kernel'
    return value

  def input_release(self):
    if self.input_probe_ready:
      value = self.input_state(True)
      time.sleep(.08)
      return value

  def inject(self, values, kind):
    if self.pid():
      def ready():
        s = self.state()
        owned = bool(s.get('visible') and s.get('page_visible', True) and s.get('window_focused') and
                     not s.get('system_overlay', {}).get('panel_visible') and not s.get('keyboard'))
        if not owned:
          self.result.setdefault('foreground_failures', []).append({k: s.get(k) for k in
            ('pid', 'view', 'visible', 'page_visible', 'window_focused', 'input_focused', 'touch_input')})
          self.record()
          raise AssertionError('Touch injection stopped after foreground loss; no focus takeover attempted')
        return s if s.get('input_focused') and s.get('touch_input', {}).get('active') else None
      # Synchronous screenshot encoding can expire the 300ms input lease.
      # Await its normal renewal only while the page still owns foreground;
      # never synthesize focus, touch through a panel, or hide a held contact.
      self.wait(ready,3)
    before = self.input_release()
    command = ('PENCHAT_TOUCH_DEVICE='+shlex.quote(self.event)+' '+self.helper+' '+' '.join(map(str, values))+' 2>&1\n'
               'penchat_touch_status=$?\nprintf "\\nremote_exit=%s\\n" "$penchat_touch_status"')
    receipt = self.sh(command)
    after = self.input_state()
    self.result['input_trace'].append({'kind': kind, 'kernel_before': before,
                                        'receipt': receipt, 'kernel_after': after})
    self.result['input_trace'] = self.result['input_trace'][-64:]
    assert receipt.endswith('remote_exit=0'), 'Touch helper failed: '+receipt
    if after['btn_touch'] or after['active_slots']:
      self.input_release()
      raise AssertionError('Touch helper returned while kernel contact remained active')

  def touch(self, x, y):
    if not self.input_probe_ready:
      return super().touch(x, y)
    return self._tap_with_ack(lambda:self.inject(self.raw(x, y), 'tap'), .15)

  def drag(self, x, y, ex, ey):
    if not self.input_probe_ready:
      return super().drag(x, y, ex, ey)
    self.inject((*self.raw(x, y), *self.raw(ex, ey)), 'drag')
    time.sleep(.45)

  def reveal(self, get_bounds):
    for _ in range(50):
      d = self.state()
      if d.get('page', {}).get('scrolling'):
        time.sleep(.12)
        continue
      b, v = get_bounds(d), d['page']['viewport']
      top = max(v[1], d['safe_insets'][1] if self.h > self.w else v[1])
      if b[1] >= top+8 and b[3] <= v[3]-8:
        time.sleep(.12)
        settled = self.state()
        if not settled.get('page', {}).get('scrolling') and get_bounds(settled) == b:
          return b
        continue
      direction = -1 if b[3] > v[3]-8 else 1
      x = (v[0]+v[2])/2;y = v[1]+(v[3]-v[1])*(.75 if direction < 0 else .25)
      self.drag(x, y, x, y+direction*min(100, (v[3]-v[1])*.5))
    raise AssertionError('Control did not settle at a reachable position')

  def launch(self):
    # Keep the source helper's foreground stability guard. A retained Falcon
    # launcher may expose its supported Retry action after native termination.
    if self.serial == 'MEB0400008900794' and self.features == ['backup-restore'] and self.key_helper_ready:
      if self.sh('cat /sys/class/backlight/backlight/bl_power') != '0':
        power = next(block for block in self.sh('cat /proc/bus/input/devices').split('\n\n')
                     if 'Name="rk805 pwrkey"' in block)
        event = '/dev/input/'+re.search(r'Handlers=.*\b(event\d+)\b',power)[1]
        node = '/dev/input/by-path/rk805-pwrkey'
        assert self.sh('readlink -f '+node) == event
        self.sh(self.key_helper+' '+node+' 116 1 100 300')
        self.wait(lambda:self.sh('cat /sys/class/backlight/backlight/bl_power') == '0',5)
    if self.serial in ('MEA2300009007113','7G12000010905426') and self.key_helper_ready:
      backlight='/sys/class/backlight/'+('dcs-backlight' if self.serial=='7G12000010905426' else 'backlight')+'/actual_brightness'
      for _ in range(2):
        if int(self.sh('cat '+backlight)) > 5:
          break
        self.sh(self.key_helper+' /dev/input/by-path/rk805-pwrkey 116 1 100 300')
        time.sleep(.4)
      assert int(self.sh('cat '+backlight)) > 5, 'Device did not wake with its paired power key'
    if self.s6 and self.key_helper_ready and self.sh('cat /sys/class/backlight/backlight/bl_power')!='0':
      config=json.loads(self.sh('cat /usr/configs/hal_key_config.json'))['Plum_V0']
      assert int(config['power_key_code'])==116
      node='/dev/input/by-path/'+config['power_key_id']
      assert self.sh('test -c '+shlex.quote(node)+' && echo yes')=='yes'
      self.sh(self.key_helper+' '+shlex.quote(node)+' 116 1 100 300');time.sleep(.4)
      assert self.sh('cat /sys/class/backlight/backlight/bl_power')=='0','S6 did not wake; no touch permitted'
    # S6's brightness=5 can still be interactive; superclass readiness checks
    # its actual focus/evdev lease instead of treating this value as sleep.
    self.sh('miniapp_cli start '+APP['appid'])
    deadline = time.monotonic()+3
    while not self.pid() and time.monotonic() < deadline:
      time.sleep(.1)
    if not self.pid():
      self.touch(122, self.h-47)
    return super().launch()

  def smoke(self):
    d = self.state()
    assert [d['width'], d['height']] == [self.w, self.h]
    assert d['rotation'] == self.screen.get('direction', 0)
    with zipfile.ZipFile(self.package) as package:
      expected = hashlib.sha256(package.read('assets/bin/penchat-lvgl')).hexdigest()
    assert self.sh('sha256sum /proc/'+self.pid()+'/exe').split()[0] == expected
    self.result['native_sha256'] = expected
    self.capture('smoke-chat')
    self.settings()
    self.capture('smoke-settings-top')
    rows = self.state()['settings']['rows']
    assert len(rows) == 27 and 'backup_restore' in rows, ('Settings row count', len(rows), list(rows))
    reachable = []
    for name in rows:
      bounds = self.reveal(lambda d, name=name: d['settings']['rows'][name]['bounds'])
      assert bounds[3]-bounds[1]+1 >= 44, (name, bounds)
      reachable.append(name)
    self.capture('smoke-settings-bottom')
    return {'settings_rows_reachable': reachable, 'native_identity': True,
            'geometry_from_falcon': True, 'note': 'Row reachability is not settings functionality approval'}

  def chat(self):
    for _ in range(12):
      d = self.state()
      if d['navigation_open']:
        self.touch(self.w-12, self.h/2)
      elif d['view'] == 'chat':
        return d
      else:
        # Use the actual scrolling page's native back button for this scoped
        # keyboard/performance run. Edge-back has its own explicit acceptance
        # scope, rather than being mixed into every settings navigation.
        heading = d['page']['heading']
        if heading[1] < d['safe_insets'][1]:
          v = d['page']['viewport']
          self.drag(self.w/2, v[1]+(v[3]-v[1])*.25,
                    self.w/2, v[1]+(v[3]-v[1])*.8)
        else:
          scale = d['ui_scale']['applied']
          dp = lambda n: (n*scale+50)//100
          size = max(44, dp(44))
          self.touch(d['screen_safety']['navigation_x']+size/2,
                     heading[1]-dp(7)+dp(2)+size/2)
    raise AssertionError('Chat could not be reached')

  def editor_key(self, name):
    d = self.state()['editor']
    key = next(k for k in d['keys'] if k['key'] == name)
    c = d['coordinate_space']
    self.touch(key['x']*self.w/c['width'], key['y']*self.h/c['height'])

  def editor_action(self, name):
    e = self.state()['editor']
    b, c = e['actions'][name], e['coordinate_space']
    self.touch((b[0]+b[2])*self.w/(2*c['width']), (b[1]+b[3])*self.h/(2*c['height']))

  def reset_fixture(self):
    """Discard only the outer fake state before another independent feature."""
    assert self.sh('test -d '+self.saved+' && echo saved || true') == 'saved' or not self.had_data
    self.close()
    self.sh('rm -rf '+self.data+' '+self.public)
    self.sh('mkdir -p '+self.data+' '+self.public)
    with tempfile.TemporaryDirectory(prefix='penchat-release-reset-') as temp:
      fixture = pathlib.Path(temp)/'penchat.db'
      self.fixture(fixture)
      self.push(fixture, self.data+'/penchat.db')
    self.launch()

  def keyboard_review(self):
    self.chat()
    self.settings()
    self.tap(self.reveal(lambda d: d['settings']['rows']['theme']['bounds']))
    self.wait(lambda: self.state()['context_menu']['kind'] == 'choice:theme')
    self.tap(self.state()['context_menu']['choices']['light']['bounds'])
    self.wait(lambda: self.state()['appearance']['theme'] == 'light' and not self.state()['appearance']['pending'])
    d = self.chat()
    self.capture('keyboard-review-light-empty')
    if self.h > self.w:
      assert d['composer_bounds'][3]-d['composer_bounds'][1]+1 == (94*d['ui_scale']['applied']+50)//100
    # Use the textarea region of the existing capsule; the collapsed portrait
    # tool row leaves its middle available to focus the native editor.
    b = d['composer_bounds']
    self.touch((b[0]+b[2])/2, (b[1]+b[3])/2)
    self.wait(lambda: self.state()['editor']['active'] and self.state()['editor']['keys'] and not self.state()['surface_motion'])
    self.capture('keyboard-review-light-open')
    if self.state()['editor']['chinese']:
      self.editor_key('language')
    for letter in 'abcd':
      self.editor_key(letter)
    self.capture('keyboard-review-light-short')
    # Known fake text only. Text entry grows through its real key path; no IPC
    # editing bridge or user's draft is involved.
    for _ in range(3):
      self.editor_key('enter')
      for letter in 'abcdefghijk':
        self.editor_key(letter)
    d = self.state()
    self.capture('keyboard-review-light-multiline')
    if self.h > self.w:
      maximum = (282*d['ui_scale']['applied']+50)//100
      assert d['composer_bounds'][3]-d['composer_bounds'][1]+1 <= maximum, ('Portrait composer exceeded 3x single row', d['composer_bounds'], maximum)
    snapshots = [{'name': 'multiline', 'composer': d['composer_bounds'],
                  'editor': d['editor']['text_bounds'], 'time': d['diagnostic_time']}]
    action = 'hide' if 'hide' in d['editor']['actions'] else 'done'
    self.editor_action(action)
    self.wait(lambda: not self.state()['editor']['active'])
    self.capture('keyboard-review-light-hidden')
    d = self.state()
    if self.w>self.h:
      assert not d['composer_hidden']
      b=d['composer_text_bounds'];assert b[3]-b[1]+1<=3*d['composer_line_height']
      assert d['at_latest_bottom'] and d['following_latest'], 'Completion lost bottom following'
    snapshots.append({'name': 'hidden', 'composer': d['composer_bounds'], 'time': d['diagnostic_time']})
    # Retain draft while opening/closing the drawer, and verify it survives.
    before = d['draft_length']
    self.settings()
    self.chat()
    assert self.state()['draft_length'] == before
    self.capture('keyboard-review-draft-retained')
    return {'snapshots': snapshots, 'fake_draft_retained': True,
            'keyboard_brightness': 'approved by user: white solid keycaps',
            'motion_smoothness': 'pending human temporal review',
            'note': 'Static screenshots cannot establish animation frame pacing'}

  def y07_render_cost(self):
    assert self.serial == '7G12000010905426' and self.h > self.w
    d = self.chat()
    # Move safely into older plain fixture content first. Avoid a latest-bottom
    # pull-to-create or a text long press while measuring ordinary scrolling.
    b = list(d['chat_header']['reader_bounds'])
    b[3] = min(b[3], d['composer_bounds'][1]-12)
    x = (b[0]+b[2])/2
    height = b[3]-b[1]
    for _ in range(12):
      d = self.state()
      if d['scroll_y'] > height*.7 and d['scroll_bottom'] > height*.7:
        break
      # A fast release can fling through several viewports. This untimed
      # preparation deliberately moves slowly and checks its settled location.
      start, end = (.35, .6) if d['scroll_bottom'] <= height*.7 else (.65, .4)
      self.inject((*self.raw(x, b[1]+height*start),
                   *self.raw(x, b[1]+height*end), 120), 'reader-prepare-slow-drag')
      time.sleep(.35)
    d = self.state()
    assert d['visible'] and d['input_focused'] and d['touch_input']['active'], 'Reader preparation lost foreground input'
    assert not d['at_latest_bottom'] and not d['editor']['active'] and not d['navigation_open']
    assert d['scroll_y'] > height*.7 and d['scroll_bottom'] > height*.7, 'Reader fixture has insufficient middle content'
    points = [*self.raw(x, b[1]+height*.35), *self.raw(x, b[1]+height*.65)]
    forward = ' '.join(map(str, points))
    reverse = ' '.join(map(str, points[2:]+points[:2]))
    # A single adb shell owns the whole measured workload. Each helper emits
    # complete type-B contact frames. There are no signals, screenshots, SQL
    # snapshots or diagnostic reads inside this loop.
    workload = ('set -e\nexport PENCHAT_TOUCH_DEVICE='+shlex.quote(self.event)+'\n'
                'for cycle in 1 2 3 4 5 6 7 8\ndo\n'
                +self.helper+' '+forward+' 16 2>/dev/null\n'
                +self.helper+' '+reverse+' 16 2>/dev/null\ndone')
    time.sleep(.6)
    before = self.state()
    assert 'render_timing' in before and 'surface_snapshot_timing' in before, 'Package lacks render/surface-snapshot counter interfaces'
    start = time.monotonic()
    self.sh(workload)
    workload_seconds = time.monotonic()-start
    after = self.state()
    assert after['visible'] and after['input_focused'] and after['touch_input']['active'], 'Reader measurement interrupted by lifecycle'
    assert after['pid'] == before['pid'] and after['session'] == before['session']
    assert not after['editor']['active'] and after['view'] == 'chat'
    evidence = render_window(before['render_timing'], after['render_timing'], before['refresh_rate']['applied'])
    evidence['surface_snapshot_timing'] = snapshot_window(before['surface_snapshot_timing'], after['surface_snapshot_timing'])
    evidence.update(workload='16 alternating real reader drags', workload_seconds=workload_seconds,
                    scroll_y_before=before['scroll_y'], scroll_y_after=after['scroll_y'],
                    visual_smoothness='pending user judgement')
    (self.out/'y07-render-cost-boundaries.json').write_text(json.dumps({
      'before': before['render_timing'], 'after': after['render_timing'],
      'surface_snapshot_before': before['surface_snapshot_timing'],
      'surface_snapshot_after': after['surface_snapshot_timing'], 'evidence': evidence,
    }, ensure_ascii=False, indent=2)+'\n')
    # Only after the measurement is complete, retain a key visual frame.
    self.capture('y07-reader-after-render-cost')
    scenarios = {'reader': evidence}

    def tap_command(x, y):
      return self.helper+' '+' '.join(map(str, self.raw(x, y)))+' 2>/dev/null'

    def transitions(name, workload, settled):
      time.sleep(.6)
      start = self.state()
      assert start['visible'] and start['input_focused'] and start['touch_input']['active'], name+' has no foreground input before measurement'
      t0 = time.monotonic()
      receipt = self.sh(workload+'\nprintf "penchat_cost_workload_complete\\n"')
      seconds = time.monotonic()-t0
      end = self.state()
      result = render_window(start['render_timing'], end['render_timing'], start['refresh_rate']['applied'])
      result['surface_snapshot_timing'] = snapshot_window(start['surface_snapshot_timing'], end['surface_snapshot_timing'])
      foreground = bool(end['visible'] and end['input_focused'] and end['touch_input']['active'])
      identity = start['pid'] == end['pid'] and start['session'] == end['session']
      expected_state = bool(settled(end))
      complete = receipt.endswith('penchat_cost_workload_complete')
      result.update(workload_seconds=seconds, visual_smoothness='pending user judgement',
                    workload_complete=complete, expected_final_state=expected_state,
                    foreground_retained=foreground, process_session_retained=identity,
                    kernel_after=self.input_state())
      state_fields = ('pid', 'session', 'view', 'visible', 'page_visible', 'window_focused', 'input_focused',
                      'navigation_open', 'keyboard', 'draft_length', 'composer_bounds')
      (self.out/(name+'-render-cost-boundaries.json')).write_text(json.dumps({
        'before': start['render_timing'], 'after': end['render_timing'],
        'surface_snapshot_before': start['surface_snapshot_timing'],
        'surface_snapshot_after': end['surface_snapshot_timing'],
        'state_before': {k: start.get(k) for k in state_fields},
        'state_after': {k: end.get(k) for k in state_fields}, 'evidence': result,
      }, ensure_ascii=False, indent=2)+'\n')
      assert foreground, name+' measurement interrupted by lifecycle'
      assert identity, name+' process/session changed during measurement'
      assert complete, name+' touch workload did not finish all commands'
      assert expected_state, name+' did not return to its expected final state'
      self.capture(name+'-after-render-cost')
      return result

    # A native editor can become active before asynchronous scanner release,
    # key construction and the entrance animation settle. Validate three whole
    # pairs outside the measured window, then freeze real resting targets and
    # use conservative, measured settling gaps with no intervening diagnostics.
    validation = []
    focus_points, finish_points = [], []
    delays = []
    for cycle in range(3):
      d = self.state()
      assert d['visible'] and d['input_focused'] and d['touch_input']['active'], 'Keyboard validation lost foreground'
      assert not d['editor']['active'] and not d['draft_length']
      capsule = d['composer_bounds']
      focus = ((capsule[0]+capsule[2])/2, (capsule[1]+capsule[3])/2)
      focus_points.append(focus)
      t0 = time.monotonic()
      self.touch(*focus)
      opened = self.wait(lambda: (s if (s := self.state())['editor']['active'] and
        s['editor']['keys'] and 'done' in s['editor']['actions'] and not s['editor'].get('ocr_preparing') and
        s['visible'] and s['input_focused'] and s['touch_input']['active'] else None), 25)
      time.sleep(.5)
      opened = self.state()
      delays.append(time.monotonic()-t0)
      editor = opened['editor']
      assert editor['active'] and not editor['length'] and not editor['preedit_length']
      done, c = editor['actions']['done'], editor['coordinate_space']
      finish = ((done[0]+done[2])*self.w/(2*c['width']),
                (done[1]+done[3])*self.h/(2*c['height']))
      finish_points.append(finish)
      t0 = time.monotonic()
      self.touch(*finish)
      closed = self.wait(lambda: (s if not (s := self.state())['editor']['active'] and
        not s['keyboard'] and not s.get('ocr_release_pending') else None), 25)
      time.sleep(.5)
      closed = self.state()
      delays.append(time.monotonic()-t0)
      assert not closed['draft_length'], 'Keyboard validation typed through an unsettled surface'
      validation.append({'cycle': cycle, 'focus': focus, 'finish': finish,
        'opened_composer': opened['composer_bounds'], 'closed_composer': closed['composer_bounds'],
        'opened_snapshot': opened['surface_snapshot_timing'], 'closed_snapshot': closed['surface_snapshot_timing'],
        'opened_keyboard_session': editor.get('keyboard_session'),
        'closed_foreground': bool(closed['visible'] and closed['input_focused'])})
    assert len(set(focus_points)) == 1 and len(set(finish_points)) == 1, 'Keyboard targets changed across validated cycles'
    settle = max(1.0, max(delays)+.3)
    (self.out/'y07-keyboard-validation.json').write_text(json.dumps({'cycles': validation,
      'observed_open_close_seconds_including_boundary_diagnostics': delays, 'workload_settle_seconds': settle},
      ensure_ascii=False, indent=2)+'\n')
    prefix = 'set -e\nexport PENCHAT_TOUCH_DEVICE='+shlex.quote(self.event)+'\n'
    keyboard_workload = (prefix+'for cycle in 1 2 3 4 5 6 7 8\ndo\n'
                         +tap_command(*focus)+'\nsleep '+str(round(settle, 3))+'\n'
                         +tap_command(*finish)+'\nsleep '+str(round(settle, 3))+'\ndone')
    scenarios['keyboard_open_hide'] = transitions('y07-keyboard', keyboard_workload,
      lambda d: not d['editor']['active'] and not d['keyboard'] and d['draft_length'] == 0)
    scenarios['keyboard_open_hide']['workload'] = '8 open/hide pairs with a real empty composer'
    scenarios['keyboard_open_hide']['settle_seconds'] = settle
    assert scenarios['keyboard_open_hide']['surface_snapshot_timing']['snapshot_count'] == 8, 'Keyboard workload did not produce eight verified exits'

    d = self.state()
    scale = d['ui_scale']['applied']
    control = max(44, (48*scale+50)//100)
    navigation = (d['screen_safety']['navigation_x']+control/2,
                  d['safe_insets'][1]+control/2)
    backdrop = (self.w-12, self.h/2)
    drawer_validation = []
    for cycle in range(2):
      self.touch(*navigation)
      self.wait(lambda: self.state()['navigation_open'])
      time.sleep(.6)
      opened = self.state()
      assert opened['visible'] and opened['input_focused'], 'Drawer validation lost foreground'
      self.touch(*backdrop)
      self.wait(lambda: not self.state()['navigation_open'])
      time.sleep(.6)
      closed = self.state()
      assert closed['visible'] and closed['input_focused'] and not closed['draft_length']
      drawer_validation.append({'cycle': cycle, 'opened': opened['navigation_open'],
                                'closed': closed['navigation_open']})
    (self.out/'y07-drawer-validation.json').write_text(json.dumps({'cycles': drawer_validation,
      'navigation': navigation, 'backdrop': backdrop, 'settle_seconds': 1.0}, indent=2)+'\n')
    drawer_workload = (prefix+'for cycle in 1 2 3 4 5 6 7 8\ndo\n'
                      +tap_command(*navigation)+'\nsleep 1\n'
                      +tap_command(*backdrop)+'\nsleep 1\ndone')
    scenarios['drawer_open_close'] = transitions('y07-drawer', drawer_workload,
      lambda d: not d['navigation_open'] and d['view'] == 'chat' and not d['editor']['active'])
    scenarios['drawer_open_close']['workload'] = '8 menu open/backdrop close pairs'
    assert scenarios['drawer_open_close']['surface_snapshot_timing']['snapshot_count'] == 8, 'Drawer workload did not produce eight verified exits'
    return {'scenarios': scenarios, 'visual_smoothness': 'pending user judgement',
            'hardware_refresh_measurement': False}

  def child(self, feature):
    folder = self.out/feature
    folder.mkdir(exist_ok=True)
    if (folder/'artifacts').exists():
      shutil.rmtree(folder/'artifacts')
    child_environment=None
    owned_update=feature in ('update-self-install','update-self-install-diagnostic')
    if owned_update:
      self.close()
      command=[sys.executable,str(ROOT/'tests/device/update_selftest.py'),'--serial',self.serial,'--release-owned','--package',str(self.package)]
      source=ROOT/'.work/update-selftest'/self.serial
      existing_operations=set(source.iterdir()) if source.exists() else set()
      child_environment=dict(os.environ,PENCHAT_RELEASE_OWNER_PID=str(os.getpid()),
        PENCHAT_RELEASE_SERIAL=self.serial,
        PENCHAT_RELEASE_LOCK_PATH=str(ROOT/'.work/release-0.3.1/device-locks'/(self.serial+'.lock')))
    elif feature == 'dpi':
      command = [sys.executable, str(ROOT/'tests/device/dpi_acceptance.py'),
                 '--serial', self.serial, '--package', str(self.package), '--scale-settings', '--theme-settings']
      source = ROOT/'.work/ui-unification'/('device-'+self.serial)
    elif feature == 'refresh-bottom':
      command = [sys.executable, str(ROOT/'tests/device/refresh_bottom_acceptance.py'),
                 '--serial', self.serial, '--package', str(self.package)]
      source = ROOT/'.work/refresh-bottom-device'/self.serial
    else:
      command = [sys.executable, str(ROOT/'tests/device/display_config.py'),
                 '--serial', self.serial]+(['--pinyin','--pinyin-core-only'] if feature=='pinyin-core' else ['--native-touch','--native-touch-core-only'] if feature=='native-touch-core' else ['--scanner-profile','--scanner-profile-core-only'] if feature=='scanner-profile-core' else ['--feedback'] if feature=='feedback-diagnostics' else ['--update'] if feature=='update-check' else ['--'+feature])
      source = ROOT/'.work'/('device-display-'+self.serial)
    # Child artifacts have historically fixed paths. Remove only our host-side
    # generated output before each run so stale PNG/results cannot imply a pass.
    if not owned_update and source.exists():
      shutil.rmtree(source)
    with (folder/'run.log').open('w') as log:
      child = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,env=child_environment)
      self.active_child = child
      diagnostic_started=None;diagnostic_captured=set()
      try:
        while child.poll() is None:
          if feature=='update-self-install-diagnostic':
            added=set(source.iterdir())-existing_operations if source.exists() else set()
            if len(added)==1:
              evidence=next(iter(added))/'recovery.json'
              if evidence.exists():
                owned=json.loads(evidence.read_text())
                if owned.get('status')=='starting' and diagnostic_started is None:diagnostic_started=time.monotonic()
            if diagnostic_started is not None:
              for delay,label in ((3,'early'),(20,'after-watchdog')):
                if time.monotonic()-diagnostic_started>=delay and label not in diagnostic_captured:
                  image=self.system_capture('owned-update-diagnostic-'+label)
                  self.result.setdefault('owned_update_diagnostic_images',{})[label]=str(image)
                  diagnostic_captured.add(label);self.record()
          time.sleep(.2)
      except BaseException:
        with defer_cleanup_signals():
          if not stop_owned_child(child):
            raise RuntimeError('Child did not exit; preserve isolation for recovery')
        raise
      finally:
        if owned_update and child.poll() is not None:
          added=set(source.iterdir())-existing_operations if source.exists() else set()
          if added:
            # Pessimistic before reading: malformed/missing evidence cannot
            # allow a still-running owned SDK page to outlive real restoration.
            blocker={'serial':self.serial,'operations':list(map(str,added)),
              'reason':'Owned test page cleanup not yet confirmed'}
            self.result['owned_fixture_recovery_required']=blocker
            (self.out/'owned-page-recovery.json').write_text(json.dumps(blocker,indent=2)+'\n');self.record()
            assert len(added)==1,'Multiple owned update operations appeared under one serial lock'
            source=added.pop();recovery=source/'recovery.json'
            owned=json.loads(recovery.read_text()) if recovery.exists() else {}
            blocker.update(recovery=str(recovery),owned_fixture=owned)
            if owned.get('restored'):
              del self.result['owned_fixture_recovery_required']
              (self.out/'owned-page-recovery.json').unlink()
            else:
              (self.out/'owned-page-recovery.json').write_text(json.dumps(blocker,indent=2)+'\n')
            self.record()
          else:
            assert child.returncode!=0,'Self-install exited without its durable operation record'
    if source.exists():
      shutil.copytree(source, folder/'artifacts', dirs_exist_ok=True)
      for image in sorted((folder/'artifacts').rglob('*.png')):
        self.result['screenshots'].append({'path': str(image), 'visual_review': 'pending'})
      for diagnostic in (folder/'artifacts').rglob('*failure*.json'):
        value = json.loads(diagnostic.read_text())
        if isinstance(value, dict) and 'pid' in value and not value.get('visible'):
          self.result.setdefault('foreground_failures', []).append({
            'source': str(diagnostic), **{k: value.get(k) for k in
              ('pid', 'view', 'visible', 'page_visible', 'window_focused', 'input_focused')}})
    assert child.returncode == 0, feature+' failed; review '+str(folder/'run.log')
    reports = []
    for path in sorted((folder/'artifacts').rglob('*result*.json')):
      reports.append({'path': str(path), 'data': json.loads(path.read_text())})
    assert reports, feature+' produced no result evidence'
    return {'functional_harness_completed': True, 'reports': reports,
            'visual_review': 'pending', 'paper_quality_validated': False}

  def require_reaped_child(self):
    child = getattr(self, 'active_child', None)
    if child is not None and child.poll() is None:
      self.result.update(restored=False, recovery_errors=['Owned child still alive; all isolation retained'],
        live_child_pid=child.pid)
      (self.out/'live-child-recovery.json').write_text(json.dumps({
        'pid': child.pid, 'serial': self.serial,
        'reason': 'Do not start another scope until this child has exited and saved trees are recovered.'}, indent=2)+'\n')
      self.record()
      raise RuntimeError('Owned child still alive; no device cleanup or original restoration attempted')

  def quarantine_unsettled_fixture(self, stopped):
    if not self.testing_payload_installed:
      return False
    closes=self.result.get('lifecycle_closes',[])
    forced=bool(closes and closes[-1].get('forced_term'))
    unexpected=False
    if stopped and self.result.get('error'):
      try:
        raw=self.sh('cat '+self.data+'/last-exit.json 2>/dev/null || true')
        exit_state=json.loads(raw) if raw else {}
        unexpected=exit_state.get('reason')!='explicit_exit'
      except (ValueError,subprocess.CalledProcessError):
        unexpected=True
    if not forced and not unexpected:
      return False
    # A failed Falcon page can retain an asynchronous rescue reporter after the
    # native child stops. Keep its entire fake tree in place until page teardown
    # is proved, so a late callback cannot write into the user's restored tree.
    self.result['owned_fixture_recovery_required']=True
    write_json(self.out/'owned-page-recovery.json',{
      'serial':self.serial,'reason':'Native failure/forced termination needs owned Falcon page teardown before data restoration',
      'forced_term':forced,'unexpected_exit':unexpected,'data':self.data,
      'saved':self.saved,'public_saved':self.public_saved})
    return True

  def require_owned_page_settled(self):
    if self.result.get('owned_fixture_recovery_required'):
      self.result.update(restored=False,recovery_errors=['Owned test AppID page cleanup unconfirmed; isolation retained'])
      self.record()
      raise RuntimeError('Owned test page may still be alive; no original restoration or foreground takeover attempted')

  def run(self):
    require_space(self.out)
    leases = ROOT/'.work/release-0.3.1/device-locks'
    leases.mkdir(parents=True, exist_ok=True)
    with (leases/(self.serial+'.lock')).open('a') as lease:
      try:
        fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
      except BlockingIOError:
        raise RuntimeError('Another isolated runner still owns device '+self.serial)
      for name in ('live-child-recovery.json','owned-page-recovery.json'):
        blocker=self.out/name
        if blocker.exists():
          raise RuntimeError('Unconfirmed child/page requires explicit recovery before any new scope: '+str(blocker))
      previous=self.out/'result.json'
      if previous.exists():
        prior=json.loads(previous.read_text())
        names='-'.join(c.get('feature','unknown') for c in prior.get('checks',[])) or 'preflight'
        stamp=str(previous.stat().st_mtime_ns)
        history=self.out/'history'/(stamp+'-'+re.sub('[^a-zA-Z0-9-]','-',names)[:100])
        history.mkdir(parents=True,exist_ok=True)
        recovery=self.out/'recovery.json'
        cutoff=(recovery.stat().st_mtime if recovery.exists() else previous.stat().st_mtime)-10
        copied=[]
        for source in self.out.iterdir():
          if source.is_file() and (source.name in ('result.json','recovery.json') or source.stat().st_mtime>=cutoff):
            shutil.copy2(source,history/source.name);copied.append(source.name)
        (history/'artifact-index.json').write_text(json.dumps({'original_directory':str(self.out),
          'archived_files':copied,'note':'Archive created under device lock before the next isolated scope; original result paths resolve by filename here.'},indent=2)+'\n')
      return self._run_isolated()

  def _run_isolated(self):
    self.screen = json.loads(self.sh('cat /etc/miniapp/resources/cfg.json'))['screen']
    self.w, self.h = self.screen['width'], self.screen['height']
    if self.s6 and self.screen.get('direction') in (90,270):self.w,self.h=self.h,self.w
    assert self.sh('uname -m') == ('armv7l' if self.s6 else 'aarch64')
    self.preflight(discard_input=self.discard_input,discard_images=getattr(self,'discard_image_input',False))
    self.host = self.sh('pidof miniapp')
    self.boot = self.sh('cat /proc/sys/kernel/random/boot_id')
    assert self.host, 'MiniApp host is not running; runner never starts/restarts it'
    # Existing feature isolation must be recovered rather than hidden by another
    # wrapper. All locations belong only to this app and this public transfer root.
    candidates = [self.saved, self.public_saved]
    for suffix in ('display-acceptance-saved', 'dpi-acceptance-saved', 'refresh-bottom-saved', 'release-0.3.1-saved', 'backup-restore-acceptance-saved', RELEASE+'-saved'):
      candidates.extend((self.data+'.'+suffix, self.public+'.'+suffix))
    assert self.sh(' && '.join('test ! -e '+shlex.quote(p) for p in candidates)+' && echo safe') == 'safe', 'Prior fixture requires recovery'
    had_data = self.sh('test -d '+self.data+' && echo yes || true') == 'yes'
    self.had_data = had_data
    had_public = self.sh('test -d '+self.public+' && echo yes || true') == 'yes'
    public_hashes = self.hashes(self.public) if had_public else []
    private_hashes = []
    private = public = False
    recovery = {'serial': self.serial, 'device': self.name, 'host': self.host, 'boot': self.boot,
                'data': self.data, 'saved': self.saved, 'public': self.public,
                'public_saved': self.public_saved, 'had_data': had_data, 'had_public': had_public,
                'public_hashes': public_hashes}
    def save_recovery():
      write_json(self.out/'recovery.json',recovery)
    save_recovery()
    try:
      # Shield public exports before normal shutdown may retire its 导出 subtree.
      if had_public:
        self.sh('mv '+self.public+' '+self.public_saved)
      public = True
      self.sh('mkdir -p '+self.public)
      self.close()
      private_hashes = self.hashes(self.data) if had_data else []
      recovery['private_hashes'] = private_hashes
      save_recovery()
      if had_data:
        self.sh('mv '+self.data+' '+self.saved)
      private = True
      self.sh('mkdir -p '+self.data)
      with tempfile.TemporaryDirectory(prefix='penchat-release-') as temp:
        folder = pathlib.Path(temp)
        if self.features == ['backup-restore']:
          from backup_restore_acceptance import prepare_fixture
          prepare_fixture(self, folder/'penchat.db')
        else:
          self.fixture(folder/'penchat.db')
        if self.features == ['scan-original-codec']:
          from release_scan_original import codec_fixture
          codec_fixture(self,folder)
        if self.features==['real-background-scanner']:
          import sqlite3
          with sqlite3.connect(folder/'penchat.db') as db:
            db.execute('delete from chat_messages');db.execute('delete from chat_sessions')
            db.execute('update app_settings set value_json=? where key=?',('""','active_session_id'))
          self.result['scanner_format_before_probe']=None if self.s6 else self.sh('v4l2-ctl -d /dev/video23 --get-fmt-video')
        if self.features in (['real-background-generation'],['official-editing'],['official-editing-dark']):
          from http_pool_flow import DeviceServer
          import sqlite3
          self.background_server=DeviceServer(ROOT,self.serial,self.out,stream=True)
          with sqlite3.connect(folder/'penchat.db') as db:
            db.execute('update providers set base_url=?,metadata_json=?',
              ('http://127.0.0.1:'+str(self.background_server.server_port)+'/v1',json.dumps({'api':'responses'})))
        if self.features == ['preset-camera-large-font']:
          from release_product_review import configure_fixture
          configure_fixture(folder/'penchat.db')
        if self.features in (['owned-official-drm'],['owned-official-readback'],['official-editing'],['official-editing-dark']):
          import sqlite3
          with sqlite3.connect(folder/'penchat.db') as db:
            db.execute('insert or replace into app_settings values(?,?,1)',('prefer_official_keyboard','true'))
            if self.features in (['official-editing'],['official-editing-dark']):db.execute('insert or replace into app_settings values(?,?,1)',('theme_mode',json.dumps('dark' if self.features==['official-editing-dark'] else 'light')))
        self.push(folder/'penchat.db', self.data+'/penchat.db')
        if self.features in (['y07-render-cost'], ['real-background-probe'], ['real-background-generation'], ['owned-official-ime'], ['y07-scroll-surface'], ['y07-scroll-dark'], ['owned-official-default'], ['owned-official-drm'], ['owned-official-readback'], ['official-editing'],['official-editing-dark']):
          # Restore a middle reading anchor in the private fixture before
          # measuring. Real touch workloads below still exercise scrolling.
          restore = {'appid': APP['appid'], 'target': APP['version'], 'snapshot': {
            'session': 'refresh-bottom', 'draft': 'penchatbackgroundfixture' if self.features in (['real-background-probe'],['real-background-generation']) else self.official_fake_text if self.features in (['owned-official-ime'],['owned-official-default'],['owned-official-drm'],['owned-official-readback'],['official-editing'],['official-editing-dark']) else '', 'preferences': {},
            'scroll': 1800, 'offset': 0, 'following': False}}
          (folder/'restore.json').write_text(json.dumps(restore))
          self.sh('mkdir -p '+self.data+'/.updates')
          self.push(folder/'restore.json', self.data+'/.updates/restore.json')
        compiler = subprocess.check_output(['bash', '-c', 'source scripts/native-target.sh '+self.target+'; printf "%s" "$PENCHAT_BUILD_CXX"'], cwd=ROOT, text=True)
        helper = folder/'tap'
        subprocess.run([compiler, '-std=c++11', '-Os', str(ROOT/'tests/device/tap.cpp'), '-o', str(helper)], check=True)
        self.push(helper, self.helper)
        probe = folder/'input-state'
        subprocess.run([compiler, '-std=c++11', '-Os', str(ROOT/'tests/device/release_input_state.cpp'), '-o', str(probe)], check=True)
        self.push(probe, self.input_probe)
        key = folder/'key'
        subprocess.run([compiler, '-std=c++11', '-Os', str(ROOT/'tests/device/key.cpp'), '-o', str(key)], check=True)
        self.push(key, self.key_helper)
      self.sh('chmod 755 '+self.helper)
      self.sh('chmod 755 '+self.input_probe)
      self.sh('chmod 755 '+self.key_helper)
      self.key_helper_ready = True
      self.event = self.sh(self.helper+' --discover')
      assert self.event.startswith('/dev/input/event') and self.event.rsplit('event', 1)[1].isdigit()
      self.input_probe_ready = True
      self.input_release()
      self.push(self.package, self.remote_package)
      assert self.sh('sha256sum '+self.remote_package).split()[0] == self.result['package_sha256']
      assert '"ret": 0' in self.sh('miniapp_cli install '+self.remote_package)
      if self.official_readback_probe:
        self.readback_remote='/tmp/penchat-drm-readback'
        sdk=pathlib.Path(os.environ.get('RK_SDK_ROOT','/home/ls-jl/dictpen/x7pro/android13_sdk/RK3562_Android13.0_SDK'))
        binary=self.out/'drm-readback'
        subprocess.run([compiler,'-std=c++14','-Os','-static-libstdc++','-static-libgcc','-I'+str(sdk/'external/libdrm/include/drm'),str(ROOT/'tests/device/drm_readback.cpp'),'-o',str(binary)],check=True)
        self.push(binary, self.readback_remote)
        self.sh('chmod 700 '+self.readback_remote)
      self.testing_payload_installed = True
      try:
        started = self.launch()
        if self.startup_rollback_guard:
          self.wait(lambda:self.state()['render_timing']['presented'] > 1)
          assert self.state()['render_timing']['presented'] > 1, 'Candidate has not progressed past its first pending flip'
        self.result['candidate_startup_verified'] = True
      except BaseException:
        if self.startup_rollback_guard:
          self.result['owned_fixture_recovery_required'] = True
          self.result['candidate_startup_verified'] = False
          (self.out/'owned-page-recovery.json').write_text(json.dumps({
            'serial': self.serial, 'reason': 'Candidate startup/presentation failed; keep original data isolated until owned page unload and verified full ordinary-package rollback',
            'candidate_package': str(self.package), 'candidate_sha256': self.result['package_sha256'],
            'rollback_package': str(ROOT/'.work/sticker-prompt'/('8001792332371649.0_3_1-'+self.target+'.amr')),
          }, indent=2)+'\n')
          self.record()
        raise
      self.result['logical_size'] = [self.w, self.h]
      self.result['falcon_screen'] = self.screen
      for index, feature in enumerate(self.features):
        if index:
          self.reset_fixture()
        entry = {'feature': feature, 'status': 'running'}
        self.result['checks'].append(entry)
        self.record()
        try:
          if feature == 'backup-restore':
            from backup_restore_acceptance import exercise
            entry['evidence'] = exercise(self)
          elif feature == 'smoke':
            entry['evidence'] = self.smoke()
          elif feature == 'keyboard-review':
            entry['evidence'] = self.keyboard_review()
          elif feature == 'global-font-preedit':
            from release_ui_review import run as global_font_preedit
            entry['evidence'] = global_font_preedit(self)
          elif feature == 'font-persistence':
            from release_ui_review import persistence as font_persistence
            entry['evidence'] = font_persistence(self)
          elif feature == 'preedit-light-review':
            from release_ui_review import light_preedit
            entry['evidence'] = light_preedit(self)
          elif feature == 'y07-back-trace':
            from release_back_trace import run as trace_back
            entry['evidence'] = trace_back(self)
          elif feature == 'preset-camera-large-font':
            from release_product_review import run as product_large_font
            entry['evidence'] = product_large_font(self)
          elif feature in ('y07-back-free-ring','y07-back-inner-ring'):
            from release_focus_ring import run as focus_ring
            entry['evidence'] = focus_ring(self, inner=feature=='y07-back-inner-ring')
          elif feature == 'y07-back-visual':
            from release_focus_ring import visual as back_visual
            entry['evidence'] = back_visual(self)
          elif feature == 'y07-back-x34-ring':
            from release_focus_ring import run as focus_ring
            entry['evidence'] = focus_ring(self, override_x=34)
          elif feature == 'y07-hole-probe-ab':
            from release_shell_probe import run as shell_probe
            entry['evidence'] = shell_probe(self)
          elif feature == 'y07-shell-cleanup':
            from release_shell_probe import cleanup as cleanup_shell
            entry['evidence'] = cleanup_shell(self)
          elif feature == 'y07-hole-probe-b2':
            from release_shell_probe import run_b2
            entry['evidence'] = run_b2(self)
          elif feature in ('y07-native-back-validation','native-back-validation'):
            from release_back_validation import run as validate_back
            entry['evidence'] = validate_back(self)
          elif feature in ('real-background-probe','real-background-generation'):
            from release_background_probe import run as background_probe
            entry['evidence'] = background_probe(self,generation=feature=='real-background-generation')
          elif feature=='real-background-scanner':
            from release_scanner_background import run as scanner_background
            entry['evidence']=scanner_background(self)
          elif feature in ('y07-scroll-surface','y07-scroll-dark'):
            from release_scroll_surface import run as scroll_surface
            entry['evidence'] = scroll_surface(self)
          elif feature == 'owned-official-drm':
            from release_official_drm import run as official_drm
            entry['evidence'] = official_drm(self)
          elif feature in ('owned-official-ime','owned-official-default','owned-official-readback'):
            from release_official_surface import run as official_surface
            entry['evidence'] = official_surface(self)
          elif feature in ('official-editing','official-editing-dark'):
            from release_official_editing import run
            entry['evidence'] = run(self)
          elif feature == 'resource-residency':
            from release_resource_residency import run
            entry['evidence'] = run(self)
          elif feature == 'scan-original':
            from release_scan_original import run
            entry['evidence'] = run(self)
          elif feature == 'scan-confirm-latency':
            from release_scan_original import latency
            entry['evidence'] = latency(self)
          elif feature == 'scan-original-codec':
            from release_scan_original import codec
            entry['evidence'] = codec(self)
          elif feature == 'feedback-preview':
            from release_feedback_preview import run
            entry['evidence'] = run(self)
          elif feature == 'keyboard-handoff':
            from release_keyboard_handoff import run
            entry['evidence'] = run(self)
          elif feature == 'panel-snapshot':
            from release_panel_snapshot import run
            entry['evidence'] = run(self)
          elif feature == 'scanner-cycles':
            from release_scanner_cycles import run
            entry['evidence'] = run(self)
          elif feature == 'settings-controls':
            from release_settings_controls import run as settings_controls
            entry['evidence'] = settings_controls(self)
          elif feature in ('auxiliary-title','auxiliary-compression'):
            from release_auxiliary import run_title,run_compression
            entry['evidence'] = (run_title if feature=='auxiliary-title' else run_compression)(self)
          elif feature == 'y07-render-cost':
            entry['evidence'] = self.y07_render_cost()
          else:
            entry['evidence'] = self.child(feature)
          entry['status'] = 'assertions-passed-review-pending'
          # A completed child restored the outer fake DB, never the user's DB.
          self.launch()
        except BaseException as error:
          entry.update(status='needs-review', error=str(error))
          self.record()
          raise
        self.record()
      self.result['functional_selection_completed'] = True
    except BaseException as error:
      self.result['error'] = str(error)
      if private:
        # Preserve the failing fixture's exit evidence before finally removes
        # it. Reading after restoration would capture the user's previous run.
        for name in ('native-exit.json','native-fault.txt','last-exit.json','lvgl-runtime.log','lvgl-runtime.previous.log'):
          try:
            content=self.sh('tail -c 262144 '+shlex.quote(self.data+'/'+name)+' 2>/dev/null || true')
            destination=self.out/('failure-'+name)
            with destination.open('w',encoding='utf-8') as stream:
              os.chmod(destination,0o600);stream.write(content);stream.flush();os.fsync(stream.fileno())
          except Exception as capture_error:
            self.result.setdefault('failure_log_errors',[]).append(name+': '+str(capture_error))
      child = getattr(self, 'active_child', None)
      if private and (child is None or child.poll() is not None) and self.pid():
        try:
          (self.out/'failure-fixture.json').write_text(json.dumps(self.state(), ensure_ascii=False, indent=2)+'\n')
          self.capture('failure-fixture')
          self.system_capture('failure-system-fixture')
        except Exception as capture_error:
          self.result['capture_error'] = str(capture_error)
      raise
    finally:
      with defer_cleanup_signals() as deferred_signals:
        # Publish pessimistically before adb: a supervisor may interrupt adb
        # itself even though Python defers its own termination requests.
        self.result.update(restored=False, recovery_errors=['Recovery in progress; completion not verified'])
        self.record()
        errors = []
        self.require_reaped_child()
        self.require_owned_page_settled()
        if getattr(self,'background_server',None):
          try:
            self.background_server.shutdown()
          except Exception as error:
            errors.append('local stream fixture shutdown: '+str(error))
        try:
          self.input_release()
        except Exception as error:
          errors.append('touch release: '+str(error))
        try:
          self.close()
        except Exception as error:
          errors.append('native close: '+str(error))
        stopped = not self.pid()
        unsettled = self.quarantine_unsettled_fixture(stopped)
        if unsettled:errors.append('Owned Falcon failure page remains unconfirmed; original trees retained')
        # A nested recovery path must never be deleted while it still carries a
        # child fixture. Retain outer originals under their explicit saved names.
        nested = []
        if private:
          for candidate in candidates:
            if candidate not in (self.saved, self.public_saved) and self.sh('test -e '+shlex.quote(candidate)+' && echo present || true') == 'present':
              nested.append(candidate)
        if nested:
          errors.append('Nested fixture recovery required: '+', '.join(nested))
        for isolated, current, saved, existed in ((private, self.data, self.saved, had_data),
                                                   (public, self.public, self.public_saved, had_public)):
          if not isolated:
            continue
          if not stopped or nested or unsettled:
            errors.append('Retained original isolation for recovery: '+saved)
            continue
          try:
            self.sh('rm -rf '+current)
            if existed:
              self.sh('mv '+saved+' '+current)
          except Exception as error:
            errors.append('restore '+current+': '+str(error))
        self.result['restored'] = not errors and (not had_data or self.hashes(self.data) == private_hashes) and (not had_public or self.hashes(self.public) == public_hashes)
        self.result['host_unchanged'] = self.sh('pidof miniapp') == self.host and self.sh('cat /proc/sys/kernel/random/boot_id') == self.boot
        self.result['recovery_errors'] = errors
        self.record()
        if self.result['restored'] and self.result['host_unchanged']:
          self.sh('rm -f '+self.remote_package+' '+self.snapshot+' '+self.snapshot_helper)
          if self.result.get('foreground_failures'):
            self.result['defer_original_resume']=True
            self.result['resume_deferred_reason']='Unexpected foreground handoff: original data restored, no focus takeover'
          if not self.result.get('defer_original_resume'):
            self.launch()
            self.input_release()
            self.result['original_app_resumed'] = True
            with zipfile.ZipFile(self.package) as archive:
              expected_native=hashlib.sha256(archive.read('assets/bin/penchat-lvgl')).hexdigest()
            actual_native=self.sh('sha256sum /proc/'+self.pid()+'/exe').split()[0]
            self.result.update(restored_native_sha256=actual_native,
              expected_restored_native_sha256=expected_native,
              native_payload_verified_after_user_restore=actual_native==expected_native)
            self.record()
            assert actual_native==expected_native,'Restored user app is not executing the selected ordinary native payload'
          else:
            self.result['original_app_resumed'] = False
          self.sh('rm -f '+self.helper+' '+self.input_probe+' '+self.key_helper)
          if self.official_readback_probe:
            self.sh('rm -f '+self.readback_remote)
          self.record()
        else:
          raise RuntimeError('Complete original recovery needs attention: '+str(self.out/'recovery.json'))


def main():
  global OUT
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--plan', action='store_true', help='Emit coverage without invoking adb')
  parser.add_argument('--serial', choices=tuple(DEVICES))
  parser.add_argument('--package', type=pathlib.Path)
  parser.add_argument('--output-dir',type=pathlib.Path,help='Task artifact directory under .work; does not change device isolation or locks')
  parser.add_argument('--discard-pending-input', action='store_true', help='Explicitly authorized: discard unsent editor/draft; preserve saved user data')
  parser.add_argument('--discard-pending-images', action='store_true', help='Separate explicit authorization: abandon temporary scan/image work; saved DB/assets still restored')
  parser.add_argument('--official-controller-file', type=pathlib.Path, help='Append-only host command file for reconnectable owned official controls')
  parser.add_argument('--startup-rollback-guard', action='store_true', help='Retain original-data isolation on candidate startup/presentation failure until explicit full-package recovery')
  parser.add_argument('--manual-paper-scan', action='store_true', help='Wait for a user paper scan instead of injecting the pen key; scan-original scope only')
  parser.add_argument('--features', nargs='+', choices=tuple(FEATURES), default=['smoke'])
  args = parser.parse_args()
  if args.output_dir:OUT=scoped_output(args.output_dir)
  if args.plan:
    print(json.dumps({'devices': DEVICES, 'features': FEATURES,
                      'manual_pending': ['All visual screenshots need user review',
                                         'Real forward paper scan optical quality',
                                         'Real OCR text and secret-field key review',
                                         'Camera printed target pose and sharpness',
                                         'Update install/rollback requires separate package strategy'],
                      'scope_exclusions': ['S6: unsupported photo camera and multiline OCR', 'Real provider/model requests', 'MiniApp host restart']}, ensure_ascii=False, indent=2))
    return
  if not args.serial or not args.package:
    parser.error('execution requires --serial and --package')
  if args.manual_paper_scan and (args.serial != 'JDA0700006552216' or args.features not in (['scan-original'],['scan-confirm-latency'])):
    parser.error('--manual-paper-scan requires the isolated S6 scan-original scope')
  def interrupted(signum, frame):
    raise SystemExit('Interrupted; restoring isolated private/public data')
  for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
    signal.signal(number, interrupted)
  device = ReleaseDevice(args.serial, args.package, args.features, args.discard_pending_input, args.startup_rollback_guard)
  device.official_controller_file = args.official_controller_file
  device.discard_image_input = args.discard_pending_images
  device.manual_paper_scan = args.manual_paper_scan
  device.run()


if __name__ == '__main__':
  main()
