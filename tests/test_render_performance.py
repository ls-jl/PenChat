#!/usr/bin/env python3
"""Compare actual LVGL pixels/geometry against an explicitly supplied executable.

The fixture is local-only. Sweep the complete reader using real touch packets,
then exercise word selection. No historical golden manifest is used. Example:
  python tests/test_render_performance.py --baseline /tmp/before --binary /tmp/after
"""
import argparse
import hashlib
import json
import os
import pathlib
import sqlite3
import tempfile
import time

from PIL import Image, ImageChops
from image_gestures import capture
from test_ui_theme import App, fixture, t

SOURCE = (
  'Alpha **bravo** 中文 glyph metrics and wrapped paragraph. '
  + 'Repeated letters fi fl AV To punctuation，标点与中文换行。 ' * 14
  + '\n\n```cpp\n'
  + '\n'.join('const std::string row_%d = "wide code keeps horizontal extent: %s";' %
              (i, '0123456789 abcdefghijklmnopqrstuvwxyz ' * 4) for i in range(7))
  + '\n```\n\n| 名称 | Formula and description | Value |\n|:---|:---|---:|\n'
  + '\n'.join('| Row %d | $\\frac{%d}{2}+x^2$ 中文说明 | %d |' % (i, i + 1, i * 17)
              for i in range(5))
  + '\n\n$$\\int_0^1 x^2\\,dx=\\frac{1}{3}$$\n\n'
  + 'Final paragraph: geometry, selection, and inline $\\sqrt{x^2+y^2}$ remain stable. '
  + 'Repeated visible tail 中文。 ' * 8
)
GEOMETRY = ('width', 'height', 'reader_height', 'scroll_y', 'scroll_bottom',
            'chat_header', 'composer_bounds', 'composer_text_bounds',
            'composer_hidden', 'following_latest', 'table_rows', 'reading_font',
            'reply_actions', 'to_bottom_visible', 'to_bottom_y')


def geometry(d):
  result = {key: d[key] for key in GEOMETRY}
  result['text_selection'] = {key: value for key, value in d['text_selection'].items()
                              if key != 'cached_hits'}
  # Region serials are internal allocation identities, not geometry or behavior.
  result['text_selection']['regions'] = [
    {key: value for key, value in region.items() if key != 'id'}
    for region in result['text_selection']['regions']]
  return result


def swipe(a, down):
  d = a.diag()
  b = d['chat_header']['reader_bounds']
  top = max(b[1] + 12, 48)
  bottom = b[3] - 12
  if not d['composer_hidden']:
    bottom = min(bottom, d['composer_bounds'][1] - 12)
  distance = max(35, int((bottom - top) * .72))
  start, end = (top, top + distance) if down else (bottom, bottom - distance)
  x = d['width'] // 2
  for step in range(9):
    a.send('touch', x=x, y=start + (end - start) * step // 8, pressed=True)
    time.sleep(.018)
  # Stop before releasing so deterministic scroll position does not depend on inertia.
  time.sleep(.16)
  a.send('touch', x=x, y=end, pressed=False)
  time.sleep(.35)


def run(binary, target, width, height, theme, scale):
  target.mkdir(parents=True, exist_ok=True)
  with tempfile.TemporaryDirectory(prefix='penchat-render-compare-') as directory:
    p = pathlib.Path(directory)
    fixture(p)
    with sqlite3.connect(p / 'penchat.db') as db:
      db.execute("delete from chat_messages where id='u'")
      db.execute("update chat_messages set output_text=? where id='a'", (SOURCE,))
      for key, value in [('theme_mode', theme), ('ui_scale_percent', scale)]:
        db.execute('insert or replace into app_settings values(?,?,1)', (key, json.dumps(value)))
    (p / 'feedback.json').write_text('{}')
    (p / 'update.json').write_text(json.dumps({'root': 'http://127.0.0.1:1/penchat-rk/',
                                             'nodes': ['127.0.0.1'], 'auto': False}))
    os.environ.update(PENCHAT_TEST_Y07_ROUNDED='1' if height > width else '0',
                      PENCHAT_TEST_FEEDBACK_CONFIG=str(p / 'feedback.json'),
                      PENCHAT_TEST_UPDATE_CONFIG=str(p / 'update.json'))
    a = App(p, width, height, binary=binary)
    snapshots = {}
    def shot(name):
      time.sleep(.25)
      d = a.diag()
      assert not d['chat_render_pending'], d
      capture(a, target / (name + '.png'))
      snapshots[name] = geometry(d)
    try:
      time.sleep(.3)
      assert a.diag()['appearance']['theme'] == theme
      assert a.diag()['ui_scale']['applied'] == scale
      shot('initial')
      before = a.diag()['scroll_y']
      swipe(a, True)
      assert a.diag()['scroll_y'] < before, 'Fixture must exercise real reader scrolling'
      shot('scrolled')
      for _ in range(100):
        if a.diag()['scroll_y'] <= 0:
          break
        swipe(a, True)
      else:
        raise AssertionError('Reader did not reach the top')
      shot('top')
      region = next(r for r in a.diag()['text_selection']['regions'] if r['key'] == 'a')
      b = region['parts'][0]
      font = a.diag()['reading_font']['size']
      x, y = b[0] + font // 2, b[1] + font // 2
      a.long_press(x, y, .55)
      t.wait_for(lambda: a.diag()['text_selection']['menu'])
      selected = a.diag()['text_selection']
      assert selected['start'] == 0 and selected['end'] == 5, selected
      shot('selected')
      a.tap(width - 3, height // 2)
      # Sweep every viewport through wide code, tables and repeated formulae.
      previous = a.diag()['scroll_y']
      for index in range(100):
        swipe(a, False)
        current = a.diag()['scroll_y']
        shot('sweep-%02d' % index)
        d = a.diag()
        if d['scroll_bottom'] <= 0:
          break
        # Near the end the composer changes bottom padding. Use the actual
        # return control before crossing that boundary; the initial and final
        # viewports overlap this last stretch of the continuous touch sweep.
        if d['scroll_bottom'] <= d['reader_height'] * .85 and d['to_bottom_visible']:
          inset = max(6, round(10 * scale / 100))
          a.tap(d['to_bottom_x'] + inset, d['to_bottom_y'] + inset)
          t.wait_for(lambda: a.diag()['following_latest'] and a.diag()['scroll_bottom'] == 0)
          shot('bottom-return')
          break
        assert current > previous, (previous, current, d['scroll_bottom'])
        previous = current
      else:
        raise AssertionError('Reader did not reach the latest bottom')
      with sqlite3.connect(p / 'penchat.db') as db:
        assert db.execute("select output_text from chat_messages where id='a'").fetchone()[0] == SOURCE
    finally:
      a.close()
      (target / 'runtime.log').write_bytes((p / 'runtime.log').read_bytes())
      (target / 'geometry.json').write_text(json.dumps(snapshots, ensure_ascii=False, indent=2) + '\n')
    return snapshots


def compare(before, after, baseline, current):
  assert before == after, 'Geometry or selection behavior differs: %s vs %s' % (baseline, current)
  records = []
  for name in before:
    left, right = baseline / (name + '.png'), current / (name + '.png')
    with Image.open(left) as a, Image.open(right) as b:
      assert a.size == b.size and a.mode == b.mode, (left, right)
      # RGBA getbbox() defaults to alpha only. Equal opaque alpha must not
      # conceal changed RGB pixels in this exact-render regression check.
      if a.tobytes() != b.tobytes():
        diff = ImageChops.difference(a, b)
        boxes = [box for box in (band.getbbox() for band in diff.split()) if box]
        bounds = (min(box[0] for box in boxes), min(box[1] for box in boxes),
                  max(box[2] for box in boxes), max(box[3] for box in boxes))
        visible = diff.convert('RGB')
        if 'A' in diff.getbands():
          alpha = diff.getchannel('A')
          visible = ImageChops.lighter(visible, Image.merge('RGB', (alpha, alpha, alpha)))
        visible.save(current / (name + '-difference.png'))
        raise AssertionError('Pixels differ at %s (%s)' % (bounds, right))
      records.append({'state': name, 'size': list(a.size),
                      'pixel_sha256': hashlib.sha256(a.tobytes()).hexdigest()})
  return records


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--baseline', required=True, type=pathlib.Path)
  parser.add_argument('--binary', required=True, type=pathlib.Path)
  parser.add_argument('--output', type=pathlib.Path,
                      default=t.ROOT / '.work/perf-redundancy/acceptance/render')
  parser.add_argument('--scales', type=int, nargs='+', default=[75, 100, 150])
  parser.add_argument('--themes', nargs='+', default=['dark', 'light'])
  parser.add_argument('--sizes', nargs='+', default=['960x266', '640x260', '568x1210'])
  args = parser.parse_args()
  t.port = 1
  args.output.mkdir(parents=True, exist_ok=True)
  manifest = {'fixture_sha256': hashlib.sha256(SOURCE.encode()).hexdigest()}
  for key in ('baseline', 'binary'):
    path = getattr(args, key).resolve()
    manifest[key] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
  (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
  results = []
  for size in args.sizes:
    width, height = map(int, size.split('x'))
    for theme in args.themes:
      for scale in args.scales:
        name = '%s-%s-%d' % (size, theme, scale)
        baseline, current = args.output / name / 'baseline', args.output / name / 'current'
        before = run(args.baseline.resolve(), baseline, width, height, theme, scale)
        after = run(args.binary.resolve(), current, width, height, theme, scale)
        records = compare(before, after, baseline, current)
        results.append({'case': name, 'snapshots': records})
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(name, len(records), 'pixel-identical snapshots; geometry and word selection identical', flush=True)
  print('PASS:', len(results), 'layout/theme/scale combinations', flush=True)


if __name__ == '__main__':
  main()
