"""Own-app native finish and capability-verified Home for device harnesses.

Never launch an unrelated app to leave PenChat. CLI documentation does not
expose stop/close; native root back is the existing finish->$page.finish path.
"""
import json
import math
import hashlib
import os
import pathlib
import shlex
import subprocess
import time
import signal
import contextlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
ALLOWED = ('MEB0400008900794', 'MEA2300009007113', '7G12000010905426')
_contexts = {}


def install_termination_handler(message='Interrupted; restoring isolated device data'):
  def interrupted(signum, frame):
    raise SystemExit(message+' (signal '+str(signum)+')')
  for number in (signal.SIGTERM,signal.SIGINT):
    signal.signal(number,interrupted)


@contextlib.contextmanager
def defer_cleanup_signals():
  deferred=[]
  previous={number:signal.getsignal(number) for number in (signal.SIGTERM,signal.SIGINT)}
  for number in previous:
    signal.signal(number,lambda signum,frame:deferred.append(signum))
  try:
    yield deferred
  finally:
    for number,handler in previous.items():signal.signal(number,handler)


class BackgroundUnavailable(RuntimeError):
  pass


def _context(serial, shell):
  assert serial in ALLOWED, 'Lifecycle helper is restricted to Y01/Y02/Y07'
  if serial in _contexts:
    return _contexts[serial]
  compiler = subprocess.check_output(['bash', '-c', 'source scripts/native-target.sh rk3562; printf "%s" "$PENCHAT_BUILD_CXX"'], cwd=ROOT, text=True)
  folder = ROOT/'.work/release-0.3.1/lifecycle-tools'/serial
  folder.mkdir(parents=True, exist_ok=True)
  context = {'screen': json.loads(shell('cat /etc/miniapp/resources/cfg.json'))['screen']}
  context['compiler'] = compiler
  context['folder'] = folder
  for name, source in [('tap', 'tap.cpp'), ('probe', 'release_input_state.cpp'), ('home', 'release_home.cpp')]:
    local = folder/name
    subprocess.run([compiler, '-std=c++11', '-Os', str(ROOT/'tests/device'/source), '-o', str(local)], check=True)
    remote = '/tmp/penchat-lifecycle-'+name
    subprocess.run(['adb', '-s', serial, 'push', str(local), remote], check=True, capture_output=True)
    shell('chmod 700 '+remote)
    context[name] = remote
  context['event'] = shell(context['tap']+' --discover')
  assert context['event'].startswith('/dev/input/event') and context['event'].rsplit('event', 1)[1].isdigit()
  _contexts[serial] = context
  return context


def pull_database(serial, data, destination, shell):
  """Pull only this app's SQLite backup snapshot, including while it is live."""
  appid = json.loads((ROOT/'package.json').read_text())['appid']
  assert serial in ALLOWED and data.endswith('/data/mini_app/pkg/'+appid+'/data')
  context = _context(serial, shell)
  library = ROOT/'.deps/rk3562-libs/libsqlite3.so.0'
  assert shell('sha256sum /usr/lib/libsqlite3.so.0').split()[0] == hashlib.sha256(library.read_bytes()).hexdigest(), 'Unverified SQLite ABI'
  if 'snapshot' not in context:
    local = context['folder']/'snapshot'
    subprocess.run([context['compiler'], '-std=c++11', '-Os', '-Wl,--as-needed', '-I'+str(ROOT/'.deps/include'),
      str(ROOT/'tests/device/sqlite_snapshot.cpp'), str(library), '-Wl,--allow-shlib-undefined', '-o', str(local)], check=True)
    context['snapshot'] = '/tmp/penchat-lifecycle-snapshot'
    subprocess.run(['adb', '-s', serial, 'push', str(local), context['snapshot']], check=True, capture_output=True)
    shell('chmod 700 '+context['snapshot'])
  remote = '/tmp/penchat-release-db-'+str(os.getpid())+'.db'
  try:
    assert shell(context['snapshot']+' '+shlex.quote(data+'/penchat.db')+' '+remote) == 'snapshot-ready'
    subprocess.run(['adb', '-s', serial, 'pull', remote, str(destination)], check=True, capture_output=True)
    pathlib.Path(destination).chmod(0o600)
  finally:
    shell('rm -f '+remote)


def _foreground(state):
  return bool(state and state.get('visible') and state.get('page_visible', True) and
              state.get('window_focused') and state.get('input_focused', True) and state.get('touch_input', {}).get('active'))


def _raw(context, x, y):
  s = context['screen'];w, h = s['width'], s['height'];x, y = round(x), round(y)
  assert 0 <= x < w and 0 <= y < h
  rotation = s.get('tp_direction', s.get('direction', 0))
  x, y = (y, w-1-x) if rotation == 270 else (w-1-x, h-1-y) if rotation == 180 else (h-1-y, x) if rotation == 90 else (x, y)
  return x+s.get('tp_xoffset', 0), y+s.get('tp_yoffset', 0)


def free_back_start(s, w, h, inner=False):
  """Choose a visible native-back edge point outside text/editor ownership."""
  scale = s.get('ui_scale', {}).get('applied', 100)
  target = max(44, (48*scale+50)//100)
  safe = s.get('safe_insets', [0, 0, 0, 0])
  safety = s.get('screen_safety', {})
  radius = safety.get('radius', 0) if safety.get('rounded') else 0
  edge = s.get('back_navigation', {}).get('edge', max((20*scale+50)//100, round(w*.046)))
  blocked = []
  selection = s.get('text_selection') or {}
  for region in selection.get('regions', []):
    blocked.extend(region.get('parts', []))
  blocked.extend(selection.get('handles', []))
  if selection.get('menu_bounds'):
    blocked.append(selection['menu_bounds'])
  e = s.get('editor') or {}
  if e.get('active'):
    c = e.get('coordinate_space', {'width': w, 'height': h})
    transform = lambda b: [b[0]*w/c['width'], b[1]*h/c['height'], b[2]*w/c['width'], b[3]*h/c['height']]
    if e.get('text_bounds'):
      blocked.append(transform(e['text_bounds']))
    blocked.extend(transform(k['hit_bounds']) for k in e.get('keys', []))
    blocked.extend(transform(handle['bounds']) for handle in e.get('selection_handles', []))
  candidates = [safe[1]+target//2, safe[1]+target, h-safe[3]-target//2, h//2]
  candidates.extend(range(max(1, safe[1]), max(2, h-safe[3]), max(8, target//2)))
  for y in candidates:
    if not safe[1] <= y < h-safe[3]:
      continue
    corner_y = min(y, h-1-y)
    contour = radius-math.sqrt(max(0, radius*radius-(radius-corner_y)**2)) if radius and corner_y < radius else 0
    minimum_x = max(2, math.ceil(contour)+2)
    x = edge-1 if inner else minimum_x
    if x < minimum_x:
      continue
    if x > edge:
      continue
    if not any(len(b) == 4 and b[0] <= x <= b[2] and b[1] <= y <= b[3] for b in blocked):
      return x, y
  raise RuntimeError('No visible left-edge point outside text/editor ownership')


def _touch(context, shell, state, points):
  assert _foreground(state()), 'Lifecycle touch refused after foreground loss'
  neutral = json.loads(shell(context['probe']+' '+context['event']+' --release'))
  assert not neutral['btn_touch'] and not neutral['active_slots']
  values = [v for point in points for v in _raw(context, *point)]
  receipt = shell('PENCHAT_TOUCH_DEVICE='+context['event']+' '+context['tap']+' '+' '.join(map(str, values))+' 2>&1\n'
                  'penchat_lifecycle_status=$?\nprintf "\\nstatus=%s\\n" "$penchat_lifecycle_status"')
  assert receipt.endswith('status=0'), receipt
  time.sleep(.35)


def close_app(serial, appid, shell, pid, state):
  """Finish this app through its own native UI, with explicit TERM fallback."""
  assert serial in ALLOWED
  result = {'normal_finish': False, 'forced_term': False, 'reason': ''}
  native = pid()
  if not native:
    result['reason'] = 'native already stopped; launcher-page finish not proven'
    return result
  assert appid in shell("tr '\\0' ' ' < /proc/"+native+'/cmdline'), 'Refuse to stop another native owner'
  context = _context(serial, shell)
  w, h = context['screen']['width'], context['screen']['height']
  requested_root_finish = False
  try:
    for _ in range(18):
      if not pid():
        result['normal_finish'] = requested_root_finish
        if not requested_root_finish:
          result['reason'] = 'native stopped before root finish was requested'
        return result
      s = state()
      if not _foreground(s):
        result['reason'] = 'foreground lost; no touch or focus takeover attempted'
        break
      e = s.get('editor', {})
      if e.get('active'):
        action = e.get('actions', {}).get('cancel') or e.get('actions', {}).get('done')
        if action:
          c = e['coordinate_space']
          _touch(context, shell, state, [((action[0]+action[2])*w/(2*c['width']), (action[1]+action[3])*h/(2*c['height']))])
          continue
      if s.get('navigation_open'):
        _touch(context, shell, state, [(w-12, h/2)])
        continue
      menu = s.get('context_menu') or {}
      discard = menu.get('choices', {}).get('discard') if menu.get('kind') == 'choice:provider_exit' else None
      if discard:
        b = discard['bounds']
        _touch(context, shell, state, [((b[0]+b[2])/2, (b[1]+b[3])/2)])
        continue
      requested_root_finish = bool(s.get('back_navigation', {}).get('root'))
      try:
        x, y = free_back_start(s, w, h, inner='edge' in s.get('back_navigation', {}))
      except RuntimeError:
        x, y = free_back_start(s, w, h)
      result.setdefault('back_starts', []).append([x, y])
      _touch(context, shell, state, [(x, y), (min(w-1, x+max(80, w/4)), y)])
      if pid():
        after = state()
        result.setdefault('back_attempts', []).append({
          'requested_root_finish': requested_root_finish,
          'view_before': s.get('view'), 'view_after': after.get('view'),
          'policy_after': after.get('back_navigation'),
          'touch_after': {k: after.get('touch', {}).get(k) for k in
            ('pressed', 'cancelled', 'x', 'y', 'source', 'gesture')},
          'foreground_after': _foreground(after)})
    if not result['reason']:
      result['reason'] = 'native back did not finish within bounded attempts'
  except Exception as error:
    result['reason'] = 'native finish unavailable: '+str(error)
  native = pid()
  if native:
    assert appid in shell("tr '\\0' ' ' < /proc/"+native+'/cmdline')
    shell('kill -TERM '+native)
    deadline = time.monotonic()+30
    while pid() and time.monotonic() < deadline:
      time.sleep(.1)
    assert not pid(), 'Own native process did not release after TERM'
    result['forced_term'] = True
  return result


def background_env(env):
  """Use only a uniquely verified physical KEY_HOME; never another app id."""
  if not isinstance(env, dict):
    env = vars(env)
  serial = env.get('SERIAL')
  shell = env.get('shell') or env.get('adb')
  pid, state = env.get('pid'), env.get('state')
  assert serial in ALLOWED and shell and pid and state
  assert _foreground(state()), 'Background action refused after foreground loss'
  context = _context(serial, shell)
  capability = json.loads(shell(context['home']+' --discover'))
  if not capability.get('supported'):
    raise BackgroundUnavailable('No uniquely verified physical KEY_HOME; background acceptance pending')
  assert shell(context['home']+' --send '+shlex.quote(capability['path'])) == 'verified-home-paired'
  time.sleep(.5)
  if not pid():
    raise BackgroundUnavailable('Verified system Home finished this native app; retained-process background acceptance remains pending')
  deadline = time.monotonic()+10
  while time.monotonic() < deadline:
    if not pid():
      raise BackgroundUnavailable('Verified system Home finished this native app; retained-process background acceptance remains pending')
    if not state().get('visible'):
      return {'verified_system_home': True, 'native_retained': True}
    time.sleep(.1)
  raise BackgroundUnavailable('Verified Home did not background PenChat; no unrelated app will be launched')
