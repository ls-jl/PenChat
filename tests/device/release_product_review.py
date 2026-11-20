"""Preset behavior and 150% preset/toast/camera caption review, fake DB only."""
import hashlib
import json
import pathlib
import sqlite3
import subprocess
import time


def configure_fixture(path):
  with sqlite3.connect(path) as db:
    db.execute("update model_configs set display_name=? where id='a'", ('用于验证预设摘要省略和完整字体高度的长模型名称 '+('Model Long '*6),))
    capabilities = {'reasoning': {'levels': {'max': 'max', 'high': 'high'}}}
    db.execute("insert into model_configs(id,provider_id,name,display_name,model_type,capabilities_json,created_at,updated_at) values('b','p','fixture-expert','独立专家模型 · 长名称摘要验证','chat',?,1,1)", (json.dumps(capabilities),))
    presets = [{'id': 'default', 'name': '默认预设', 'model_id': 'a', 'effort': 'api_default'},
      {'id': 'expert', 'name': '复杂公式与英文段落的专家预设', 'model_id': 'b', 'model_when': 'always',
       'effort': 'max', 'effort_when': 'always', 'prompt_mode': 'custom', 'prompt_when': 'always',
       'prompt': 'Local fixture reference only; no model request is made.'}]
    for key, value in [('reading_font_percent', 150), ('theme_mode', 'light'),
                       ('chat_presets', presets), ('presets_enabled', True),
                       ('camera_auto_orientation', False), ('camera_orientation', 0)]:
      db.execute('insert or replace into app_settings values(?,?,1)', (key, json.dumps(value)))


def run(d):
  assert d.state()['reading_font']['percent'] == 150
  assert d.state()['ui_scale']['applied'] == 100

  def caption_layout(kind):
    value = d.state()['presets_layout']
    assert value['kind'] == kind and value['labels']
    for label in value['labels']:
      b = label['bounds']
      assert b[3]-b[1]+1 >= label['line_height'], ('Preset caption clipped vertically', label)
      if kind == 'page' and label['role'] == 'detail':
        row = label['row_bounds']
        assert b[1] >= row[1] and b[3] <= row[3], ('Preset summary escaped its row', label)
    return value

  def attachment(name):
    if not d.state()['attachments']:
      d.tap(d.state()['attachment_button'])
      d.wait(lambda: d.state()['attachments'])
    for _ in range(30):
      s = d.state();b, v = s['attachment_actions'][name], s['attachment_viewport']
      if b[1] >= v[1] and b[3] <= v[3]:
        d.tap(b)
        return
      x = (v[0]+v[2])/2;start = v[1]+(v[3]-v[1])*.75
      d.drag(x, start, x, max(v[1]+8, start-100))
    raise AssertionError('Attachment action not reachable: '+name)

  d.chat();d.settings()
  d.tap(d.reveal(lambda s: s['settings']['rows']['presets']['bounds']))
  d.wait(lambda: d.state()['view'] == 'presets')
  page = caption_layout('page')
  d.capture('preset-font150-page')
  d.chat();attachment('presets')
  d.wait(lambda: d.state()['popup'] == 'presets')
  menu = caption_layout('menu')
  d.capture('preset-font150-menu')
  for _ in range(20):
    menu = d.state()['presets_layout']
    row = next(v for v in menu['labels'] if v['role'] == 'title' and v['index'] == 1)['row_bounds']
    v = menu['bounds']
    if row[1] >= v[1] and row[3] <= v[3]:
      d.tap(row)
      break
    x = (v[0]+v[2])/2;d.drag(x, v[3]-12, x, max(v[1]+12, v[3]-110))
  s = d.wait(lambda: (s if (s := d.state())['active_model'] == 'b' and not s['attachments'] else None))
  assert s['effective_effort'] == 'max'
  # Copy uses an existing fake reply and produces the actual 150% toast.
  for _ in range(30):
    s = d.state();row = next(iter(s['reply_actions']['rows'].values()))
    b = row['copy'];v = s['chat_header']['reader_bounds']
    if row['visible'] and b[1] >= v[1] and b[3] < s['composer_bounds'][1]:
      d.tap(b)
      break
    if s['to_bottom_visible']:
      d.touch(s['to_bottom_x']+22, s['to_bottom_y']+22)
    else:
      d.drag((v[0]+v[2])/2, v[3]-80, (v[0]+v[2])/2, max(v[1]+16, v[3]-160))
  d.capture('reply-toast-font150')
  assert d.state()['editor']['clipboard_count'] == 1
  with_folder = d.out/'preset-data-snapshot.db'
  from release_lifecycle import pull_database
  pull_database(d.serial, d.data, with_folder, d.sh)
  with sqlite3.connect(with_folder) as db:
    assert db.execute('select count(*) from chat_sessions').fetchone()[0] == 1
    assert db.execute('select count(*) from chat_messages').fetchone()[0] == 1

  def album_signature():
    command = r"""if test -d /userdisk/Pictures; then
find /userdisk/Pictures -maxdepth 1 -type f -exec stat -c '%n:%s:%Y' {} \;
else
printf absent
fi"""
    data = d.sh(command)
    return hashlib.sha256('\n'.join(sorted(data.splitlines())).encode()).hexdigest()

  album_before = album_signature()
  attachment('camera')
  d.wait(lambda: d.state()['view'] == 'camera' and d.state()['camera_ready'], 30)
  assert d.state()['camera_angle'] == 0
  d.capture('camera-caption-font150')
  d.tap(d.state()['camera_controls']['shutter'])
  crop = d.wait(lambda: (s if (s := d.state())['view'] == 'crop' and s['crop_loaded'] else None), 30)
  assert not crop['crop_save_album'] and min(crop['crop_source']) > 0
  if d.serial == '7G12000010905426':
    assert sorted(crop['crop_source']) == [2448, 3264]
  d.capture('crop-caption-font150')
  d.tap(crop['crop_actions']['cancel'])
  d.wait(lambda: d.state()['view'] == 'camera')
  assert not d.state()['generating']
  d.close();d.launch()
  assert album_signature() == album_before
  assert not d.sh('find '+d.data+'/camera-tmp -type f 2>/dev/null || true')
  result = {'text_percent': 150, 'ui_scale': 100, 'preset_page': page, 'preset_menu': menu,
    'expert_model_and_effort_applied': True, 'no_new_message_or_session': True,
    'copy_toast_clipboard_count': 1, 'camera_source': crop['crop_source'],
    'camera_capture_cancel_without_send': True, 'album_metadata_unchanged': True,
    'temporary_capture_cleaned': True, 'visual_review': 'pending', 'optical_quality': 'not validated'}
  (d.out/'preset-camera-large-font-results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
  return result
