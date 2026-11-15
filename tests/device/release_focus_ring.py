"""Single-gesture numeric focus-ring sampling; no strace or screenshots."""
import json
import subprocess
import time

from release_lifecycle import free_back_start


def run(d, inner=False, override_x=None):
  assert d.serial == '7G12000010905426'
  s = d.chat()
  assert s['visible'] and s.get('page_visible') and s['window_focused'] and s['input_focused'] and s['touch_input']['active']
  assert 'input_focus_trace' in s, 'This package does not contain bounded numeric focus diagnostics'
  outer = free_back_start(s, d.w, d.h)
  x, y = free_back_start(s, d.w, d.h, inner=inner)
  assert y == outer[1], 'Position comparison must use the same unoccupied title band'
  edge = s['back_navigation'].get('edge',max((20*s['ui_scale']['applied']+50)//100,round(d.w*.046)))
  if override_x is not None:
    x = override_x
    assert x > edge and x >= outer[0], 'Outside-band comparison must be beyond native edge but within visible glass'
    assert not any(b[0] <= x <= b[2] and b[1] <= y <= b[3] for r in s['text_selection']['regions'] for b in r['parts']), 'Comparison point belongs to readable text'
  endpoint = (min(d.w-1, x+max(80, d.w/4)), y)
  d.input_release()
  before = d.state()
  native = d.pid()
  assert before['visible'] and before['input_focused'] and before['touch_input']['active']
  points = (*d.raw(x, y), *d.raw(*endpoint))
  # No diagnostic, image encoding or UI sampling occurs during the contact.
  receipt = d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+' '.join(map(str, points))+' 2>&1\n'
                'penchat_ring_status=$?\nprintf "\\nremote_exit=%s\\n" "$penchat_ring_status"')
  time.sleep(.7)
  after = d.state() if d.pid() else None
  neutral = d.input_release()
  fields = ('pid','diagnostic_time','visible','page_visible','window_focused','input_focused','touch_input','back_navigation','touch','system_overlay','input_focus_trace')
  result = {'position': 'outside-native-edge-x'+str(x) if override_x is not None else 'inner-edge-minus-one' if inner else 'free-visible-edge',
    'start': [x,y], 'outer_reference': list(outer), 'end': list(endpoint),
    'native_pid': native, 'root_finish_observed': after is None,
    'native_edge_px': edge, 'expected_native_back_candidate': override_x is None,
    'receipt': receipt, 'kernel_neutral': neutral,
    'before': {k: before.get(k) for k in fields},
    'after': None if after is None else {k: after.get(k) for k in fields},
    'source': 'Bounded in-memory numeric diagnostics only; no screenshot, strace, new input lease or focus takeover',
    'normal_back_approval': 'pending analysis'}
  name = 'y07-back-x'+str(x)+'-ring' if override_x is not None else 'y07-back-inner-ring' if inner else 'y07-back-free-ring'
  (d.out/(name+'.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
  assert receipt.endswith('remote_exit=0'), 'Controlled gesture did not complete'
  return result


def visual(d):
  """One held preview gesture, one Falcon frame and one native frame only."""
  assert d.serial == '7G12000010905426'
  s = d.chat()
  assert s['visible'] and s['page_visible'] and s['input_focused'] and s['touch_input']['active']
  start = free_back_start(s, d.w, d.h)
  end = (max(80, start[0]+60), start[1])
  values = (*d.raw(*start), *d.raw(*end))
  d.input_release()
  command = ('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' --move-hold '+
             ' '.join(map(str, values))+' 150 2000 2>&1')
  process = subprocess.Popen(['adb', '-s', d.serial, 'shell', command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
  try:
    # Movement lasts ~240ms after the 150ms down. Capture during its bounded
    # hold, not after release. These images deliberately perturb this visual
    # diagnostic and are never presented as timing or frame-rate evidence.
    time.sleep(.55)
    system_image=d.system_capture('y07-back-preview-system')
    d.capture('y07-back-preview-native')
  finally:
    receipt = process.communicate(timeout=6)[0]
    neutral = d.input_release()
  after = d.state() if d.pid() else None
  result = {'start': list(start), 'end': list(end), 'hold_ms': 2000,
    'system_image': str(system_image),
    'native_image': str(d.out/'y07-back-preview-native.png'),
    'receipt': receipt, 'kernel_neutral': neutral,
    'after': None if after is None else {k: after.get(k) for k in
      ('visible','page_visible','window_focused','input_focused','back_navigation','input_focus_trace')},
    'purpose': 'Single visual identification of the focus-stealing preview; not performance evidence or normal-Back approval'}
  (d.out/'y07-back-preview-visual.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
  return result
