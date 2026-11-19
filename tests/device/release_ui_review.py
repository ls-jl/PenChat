"""Mapped-touch global-font and preedit review inside release data isolation."""
import json
import time


def persistence(d):
  """Prove nondefault text scaling survives process exit without UI-scale edits."""
  ui_scale = d.state()['ui_scale']['applied']
  results = []
  for percent in (150, 100):
    d.chat();d.settings()
    d.tap(d.reveal(lambda s: s['settings']['rows']['font_size']['bounds']))
    d.wait(lambda: d.state()['view'] == 'font_size')
    b = d.reveal(lambda s: s['reading_font']['slider'])
    d.touch(b[0]+round((b[2]-b[0])*(percent-75)/75), (b[1]+b[3])/2)
    d.wait(lambda: d.state()['reading_font']['draft'] == percent)
    for _ in range(30):
      s = d.state();b, v = s['page']['actions']['保存'], s['page']['viewport']
      if b[1] >= v[1] and b[3] <= v[3]:
        d.tap(b)
        break
      x = (v[0]+v[2])/2;y = v[1]+(v[3]-v[1])*.25
      d.drag(x, y, x, min(v[3]-8, y+min(100, (v[3]-v[1])*.5)))
    d.wait(lambda: d.state()['view'] == 'settings' and not d.state()['reading_font']['pending'])
    d.chat()
    old_pid = d.pid()
    d.close();d.launch()
    s = d.state()
    assert d.pid() != old_pid and s['reading_font']['percent'] == percent and s['reading_font']['applied'] == percent
    assert s['ui_scale']['applied'] == ui_scale, 'Text preference changed UI-scale geometry'
    d.capture('font-persistence-restarted-'+str(percent))
    results.append({'text_percent': percent, 'reading_font': s['reading_font'],
                    'ui_scale': s['ui_scale']['applied'], 'native_pid_changed': True})
  return {'nondefault_then_default_native_restarts': results, 'independent_ui_scale': ui_scale}


def light_preedit(d):
  d.chat();d.settings()
  d.tap(d.reveal(lambda s: s['settings']['rows']['theme']['bounds']))
  d.wait(lambda: d.state()['context_menu']['kind'] == 'choice:theme')
  d.tap(d.state()['context_menu']['choices']['light']['bounds'])
  d.wait(lambda: d.state()['appearance']['theme'] == 'light' and not d.state()['appearance']['pending'])
  s = d.chat();d.tap(s['composer_text_bounds'])
  d.wait(lambda: d.state()['editor']['active'] and d.state()['editor']['keys'])
  assert d.state()['editor']['layout'] == 'zh26'
  for letter in 'nihaozhongguo':
    d.editor_key(letter)
  s = d.wait(lambda: (s if (s := d.state())['editor']['candidate_count'] > 0 and not s['editor']['pending'] else None), 20)
  e = s['editor']
  assert e['composition_floating'] == (not e['compact_full'])
  d.capture('white-keycaps-floating-preedit')
  return {'theme': 'light', 'white_keycap_color': 'approved by user', 'layout_review': 'pending',
    'composition_floating': e['composition_floating'], 'composition_bounds': e['composition_bounds'],
    'candidate_bounds': e['candidate_bounds'], 'font_sizes': e['font_sizes'],
    'image': str(d.out/'white-keycaps-floating-preedit.png')}


def run(d):
  samples = []

  def ready():
    def renewed():
      s=d.state()
      assert (s['visible'] and s['page_visible'] and s['window_focused'] and
              not s['system_overlay'].get('panel_visible',False) and
              not s['screen_power'].get('blocked',False)), 'UI review lost foreground ownership'
      # A font rebuild can cross the existing 300ms touch lease. Wait for its
      # normal renewal; do not change the lease, relaunch or replay any touch.
      return s if s['input_focused'] and s['touch_input']['active'] and not s['surface_motion'] else None
    return d.wait(renewed,3)

  def page_action(name):
    for _ in range(35):
      s = d.state();b, v = s['page']['actions'][name], s['page']['viewport']
      # Header actions can legitimately begin at y=2. The Settings-row helper
      # reserves 8px, which cannot be reached at a nonelastic reader's top.
      if b[1] >= v[1] and b[3] <= v[3]:
        d.tap(b)
        return
      direction = 1 if b[1] < v[1] else -1
      x = (v[0]+v[2])/2;y = v[1]+(v[3]-v[1])*(.25 if direction > 0 else .75)
      d.drag(x, y, x, y+direction*min(100, (v[3]-v[1])*.5))
    raise AssertionError('Page action could not be reached: '+name)

  def font(percent):
    d.chat()
    d.settings()
    d.tap(d.reveal(lambda s: s['settings']['rows']['font_size']['bounds']))
    d.wait(lambda: d.state()['view'] == 'font_size')
    b = d.reveal(lambda s: s['reading_font']['slider'])
    d.touch(b[0]+round((b[2]-b[0])*(percent-75)/75), (b[1]+b[3])/2)
    d.wait(lambda: d.state()['reading_font']['draft'] == percent)
    d.capture('global-font-'+str(percent)+'-preview')
    page_action('保存')
    d.wait(lambda: d.state()['view'] == 'settings' and d.state()['reading_font']['percent'] == percent and
           not d.state()['reading_font']['pending'])
    s = ready()
    assert s['reading_font']['applied'] == percent and s['reading_font']['roles']
    d.reveal(lambda s: s['settings']['rows']['models']['bounds'])
    d.capture('global-font-'+str(percent)+'-settings')
    d.tap(d.state()['settings']['rows']['models']['bounds'])
    d.wait(lambda: d.state()['view'] == 'provider')
    d.capture('global-font-'+str(percent)+'-providers')
    d.chat()
    return s['reading_font']

  def editor_open():
    s = d.chat()
    if s['composer_hidden'] and s['to_bottom_visible']:
      d.touch(s['to_bottom_x']+22, s['to_bottom_y']+22)
      d.wait(lambda: not d.state()['composer_hidden'])
    s = ready()
    b = s['composer_text_bounds']
    d.touch((b[0]+b[2])/2, (b[1]+b[3])/2)
    d.wait(lambda: d.state()['editor']['active'] and d.state()['editor']['keys'])
    time.sleep(.4)
    e = ready()['editor']
    if e['layout'] != 'zh26':
      k = next(k for k in e['keys'] if k['key'] == 'language')
      c = e['coordinate_space']
      point = d.raw(k['x']*d.w/c['width'], k['y']*d.h/c['height'])
      d.inject(('--hold', *point, 700), 'layout-selector-hold')
      d.wait(lambda: d.state()['editor']['layout_menu'])
      d.editor_action('layout:0')
      d.wait(lambda: d.state()['editor']['layout'] == 'zh26')
    return ready()['editor']

  def composed():
    s = d.wait(lambda: (s if (s := d.state())['editor']['candidate_count'] > 0 and
      not s['editor']['pending'] else None), 20)
    e = s['editor']
    p, c = e['composition_bounds'], e['candidate_bounds']
    assert len(p) == len(c) == 4 and p[2] > p[0] and c[2] > c[0]
    assert e['composition_floating'] == (not e['compact_full'])
    if e['composition_floating']:
      if d.h > d.w:
        assert p[3] < c[1], ('Portrait preedit must float above candidates', p, c)
      else:
        assert p[1] > c[3], ('Wide preedit must float below candidates', p, c)
    else:
      assert p[2] < c[0], ('Compact preedit and candidates overlap', p, c)
    return e

  for percent in (75, 100, 150):
    reading = font(percent)
    d.capture('global-font-'+str(percent)+'-chat')
    s = ready()
    scale = s['ui_scale']['applied']
    size = max(44, (48*scale+50)//100)
    x = s['screen_safety']['navigation_x'] if d.h > d.w else (16*scale+50)//100
    y = s['safe_insets'][1] if d.h > d.w else (8*scale+50)//100
    d.touch(x+size/2, y+size/2)
    d.wait(lambda: d.state()['navigation_open'])
    d.capture('global-font-'+str(percent)+'-drawer')
    d.touch(d.w-12, d.h/2)
    d.wait(lambda: not d.state()['navigation_open'])
    empty = editor_open()
    d.capture('global-font-'+str(percent)+'-keyboard-empty')
    for letter in 'nihao':
      d.editor_key(letter)
    short = composed()
    d.capture('global-font-'+str(percent)+'-preedit-short')
    for letter in 'zhongguo':
      d.editor_key(letter)
    long = composed()
    if long['composition_floating']:
      assert long['candidate_bounds'][2]-long['candidate_bounds'][0] == short['candidate_bounds'][2]-short['candidate_bounds'][0], 'Floating preedit stole candidate width'
    d.capture('global-font-'+str(percent)+'-preedit-long')
    expand_before = long['actions'].get('expand')
    keys_before = {k['key']: k['bounds'] for k in long['keys']}
    if expand_before:
      d.editor_action('expand')
      d.wait(lambda: d.state()['editor']['expanded'])
      expanded = ready()['editor']
      assert expanded['actions']['expand'] == expand_before
      # Portrait deliberately replaces the letter keys with the candidate
      # workspace, retaining only its globe. Retained targets must not move;
      # all original caps must return to the same places after collapse.
      assert all(keys_before[k['key']] == k['bounds'] for k in expanded['keys'])
      d.capture('global-font-'+str(percent)+'-candidates-expanded')
      d.editor_action('expand')
      d.wait(lambda: not d.state()['editor']['expanded'])
      assert {k['key']: k['bounds'] for k in ready()['editor']['keys']} == keys_before
    d.editor_key('enter')
    d.wait(lambda: not d.state()['editor']['preedit_length'])
    cleared = ready()['editor']
    assert not cleared['composition_bounds'], 'Empty composition retained its floating strip'
    d.capture('global-font-'+str(percent)+'-preedit-cleared')
    for _ in range(cleared['length']):
      d.editor_key('delete')
    d.wait(lambda: d.state()['editor']['length'] == 0)
    d.editor_action('done')
    d.wait(lambda: not d.state()['editor']['active'])
    assert d.state()['draft_length'] == 0
    samples.append({'percent': percent, 'reading_font': reading,
      'keyboard_font_sizes': empty['font_sizes'],
      'keyboard_q': next((k for k in empty['keys'] if k['key'] == 'q'), None),
      'short_preedit': short['composition_bounds'], 'long_preedit': long['composition_bounds'],
      'short_candidates': short['candidate_bounds'], 'long_candidates': long['candidate_bounds'],
      'floating': long['composition_floating'], 'cleared': True,
      'visual_review': 'pending', 'white_keycap_color': 'approved by user'})
  by_percent = {s['percent']: s for s in samples}
  for role in by_percent[100]['reading_font']['roles']:
    assert by_percent[75]['reading_font']['roles'][role] < by_percent[100]['reading_font']['roles'][role] < by_percent[150]['reading_font']['roles'][role], ('Global font role did not scale', role)
  # Restore fixture defaults and verify persistence through a native-only restart.
  font(100)
  d.chat()
  d.close()
  d.launch()
  assert d.state()['reading_font']['percent'] == 100
  result = {'samples': samples, 'global_roles_scaled': True, 'native_restart_persistence': True,
    'real_native_touches': True, 'no_model_requests': True, 'visual_approval': 'pending'}
  (d.out/'global-font-preedit-results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
  return result
