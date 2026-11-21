#!/usr/bin/env python3
"""Real restore UI/transactions inside complete, locked private/public isolation.

Only fake local configuration is used. No model requests, historical package
installation, host restart or unrelated feature workload is part of this scope.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sqlite3
import sys
import time
import uuid

import release_acceptance as r
from release_lifecycle import install_termination_handler, pull_database

sys.path.insert(0, str(r.ROOT/'tests'))
from test_backup_restore import Restore, sql


def manifest(folder, data_root):
  files = []
  for group in ('data', 'public'):
    for path in sorted((folder/group).rglob('*')):
      item = {'path': path.relative_to(folder).as_posix()}
      if path.is_dir():
        item['directory'] = True
      else:
        item.update(size=path.stat().st_size,
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest())
      files.append(item)
  (folder/'manifest.json').write_text(json.dumps({
    'format': 1, 'operation': folder.name, 'time_ms': int(time.time()*1000),
    'appid': r.APP['appid'], 'source_version': r.APP['version'],
    'source_schema': 3, 'data_root': data_root, 'files': files,
  }))


def prepare_fixture(d, path):
  with __import__('zipfile').ZipFile(d.package) as package:
    d.expected_native = hashlib.sha256(package.read('assets/bin/penchat-lvgl')).hexdigest()
  f = Restore()
  f.setUp()
  d.restore_fixture = f
  for tree in (f.src, f.data):
    with sqlite3.connect(tree/'penchat.db') as db:
      db.execute('update chat_sessions set metadata_json=?',
                 (json.dumps({'title_state': 'manual'}),))
      for key, value in [('auto_title_enabled', False),
                         ('auto_context_compression', False),
                         ('onboarding', {'version': 1, 'complete': True, 'step': 6})]:
        db.execute('insert or replace into app_settings values(?,?,1)',
                   (key, json.dumps(value)))
      for column in ('stored_path', 'display_src'):
        db.execute('update message_assets set '+column+'=replace('+column+',?,?)',
                   (str(tree), d.data))
      db.execute('update draft_attachments set image_json=replace(image_json,?,?)',
                 (str(tree), d.data))
  # Keep the retained fixture image outside GC's generated-name namespace;
  # Falcon's actual path spelling is verified through newly restored assets.
  picture=f.data/'assets/asset_1_1.png'
  picture.rename(f.data/'assets/fixture-current.png')
  for column in ('stored_path','display_src'):
    sql(f.data,'update message_assets set '+column+'=replace('+column+',?,?)',('asset_1_1.png','fixture-current.png'))
  f.pack()
  meta = json.loads((f.backup/'manifest.json').read_text())
  meta['data_root'] = d.data
  (f.backup/'manifest.json').write_text(json.dumps(meta))
  shutil.copy2(f.data/'penchat.db', path)
  for name in ('assets', 'ime'):
    d.push(f.data/name, d.data+'/'+name)
  d.push(f.pub/'same.txt', d.public+'/same.txt')

  catalog = f.root/'catalog'
  catalog.mkdir()
  broken = catalog/str(uuid.uuid4())
  shutil.copytree(f.backup, broken)
  meta['operation'] = broken.name
  (broken/'manifest.json').write_text(json.dumps(meta))
  (broken/'public/same.txt').write_text('damaged backup')

  mixed = catalog/str(uuid.uuid4())
  shutil.copytree(f.src, mixed/'data')
  (mixed/'public').mkdir()
  shutil.rmtree(mixed/'data/ime')
  with sqlite3.connect(mixed/'data/penchat.db') as db:
    db.execute("delete from message_parts where session_id='source-new'")
    db.execute("delete from chat_messages where session_id='source-new'")
    db.execute("delete from chat_sessions where id='source-new'")
    db.execute("delete from chat_drafts where draft_key='new'")
    db.execute('delete from app_settings')
    db.execute("update providers set is_default=0,api_key='mixed-unused-secret'")
    db.execute("update model_configs set is_default=0,display_name='Mixed model'")
    db.execute("update chat_messages set output_text='mixed restored text'")
    db.execute("update message_parts set text='mixed restored text' where part_type='text'")
    db.execute("update chat_drafts set text='mixed restored draft'")
  manifest(mixed, d.data)
  d.restore_catalog = {'full': f.backup, 'broken': broken, 'mixed': mixed}


def exercise(d):
  evidence = {'scope': 'backup-restore-only', 'cases': [], 'model_requests': False}
  d.result['backup_restore'] = evidence

  def checkpoint(name, **fields):
    evidence['cases'].append({'case': name, **fields})
    d.record()
    print(json.dumps({'serial': d.serial, 'case': name, **fields}, ensure_ascii=False), flush=True)

  def state():
    return d.state()['backup_restore']

  def control(key, direct=False):
    b = d.reveal(lambda s: s['backup_restore']['controls'][key])
    assert b[3]-b[1]+1 >= 44
    if direct:
      d.inject(d.raw((b[0]+b[2])/2, (b[1]+b[3])/2), 'restore-apply')
    else:
      d.tap(b)

  def screen(name):
    def ready():
      s=state()
      return s if s['screen']==name and not s['busy'] else None
    return d.wait(ready,40)

  def snapshot(name):
    path = d.out/(name+'.db')
    pull_database(d.serial, d.data, path, d.sh)
    with sqlite3.connect('file:'+str(path)+'?mode=ro', uri=True) as db:
      assert db.execute('pragma integrity_check').fetchone()[0] == 'ok'
      # The deferred image collector may update only its disposable work queue.
      lines=[line for line in db.iterdump() if not line.startswith('INSERT INTO "asset_gc"')]
      digest = hashlib.sha256('\n'.join(lines).encode()).hexdigest()
    return path, digest

  def query(path, command):
    with sqlite3.connect('file:'+str(path)+'?mode=ro', uri=True) as db:
      return db.execute(command).fetchall()

  def start(kind):
    screen('list')
    control('backup:'+d.restore_catalog[kind].name)
    return screen('conflict')

  def bulk(choice):
    control('all')
    assert state()['all']
    control(choice)
    s = screen('summary')
    assert set(s['decisions'].values()) == {choice}
    assert len(s['decisions']) == s['plan']['conflicts']
    return s

  def apply(name, reload=True):
    screen('summary')
    before = d.pid()
    control('apply', direct=True)
    if reload:
      d.wait(lambda: d.pid() and d.pid() != before, 60)
    d.wait(lambda: d.state().get('view') == 'backup_restore', 40)
    after = screen('list')
    assert ('完成' in after['message']) if reload else ('全部冲突已跳过' in after['message']), after
    assert (d.pid() != before) if reload else (d.pid() == before)
    d.capture(name)
    checkpoint(name, native_reloaded=reload, result_message=after['message'])
    return snapshot(name)[0]

  assert d.sh('sha256sum /proc/'+d.pid()+'/exe').split()[0] == d.expected_native
  d.settings()
  rows = d.state()['settings']['rows']
  assert 'backup_restore' in rows and 'update' in rows
  d.tap(d.reveal(lambda s: s['settings']['rows']['backup_restore']['bounds']))
  empty = screen('list')
  assert not empty['items'] and '备份并安装' in empty['message']
  d.capture('restore-empty')
  checkpoint('settings-sibling-and-empty-guidance')
  d.sh('mkdir -p '+d.data+'/.rescue/backups')
  for folder in d.restore_catalog.values():
    d.push(folder, d.data+'/.rescue/backups/'+folder.name)
  control('refresh')
  d.wait(lambda: state()['screen']=='list' and not state()['busy'] and len(state()['items'])==3,40)
  _, initial = snapshot('before')
  control('backup:'+d.restore_catalog['broken'].name)
  d.wait(lambda: state()['screen'] == 'list' and not state()['busy'] and
         ('校验' in state()['message'] or 'SHA' in state()['message']), 40)
  assert snapshot('after-rejected')[1] == initial
  d.capture('restore-corrupt-rejected')
  checkpoint('corrupt-backup-rejected-without-data-change')

  start('full')
  d.capture('restore-conflict')
  # Header actions intentionally begin above the reader helper's 8dp margin.
  d.tap(d.state()['page']['actions']['取消'])
  d.wait(lambda: d.state()['view'] == 'settings')
  assert snapshot('after-cancel')[1] == initial
  checkpoint('cancel-preview-preserves-data')
  d.tap(d.reveal(lambda s: s['settings']['rows']['backup_restore']['bounds']))
  start('full')
  bulk('skip')
  p = apply('all-skip-adds-missing')
  assert query(p, "select output_text from chat_messages where id='message-conflict'") == [('current text',)]
  assert query(p, "select count(*) from chat_sessions where id in ('current-only','source-new')") == [(2,)]
  assert query(p, "select api_key from providers where id='p'") == [('current-secret',)]
  assert d.sh('cat '+d.public+'/same.txt') == 'current file'
  assert d.sh('cat '+d.public+'/new.txt') == 'added file'

  start('full')
  # A bulk choice overrides an earlier individually selected conflict.
  control('skip')
  d.wait(lambda: state()['conflict'] == 1)
  bulk('replace')
  p = apply('all-replace-overrides-previous-choice')
  assert query(p, "select output_text from chat_messages where id='message-conflict'") == [('backup text',)]
  assert query(p, "select api_key from providers where id='p'") == [('backup-secret',)]
  assert query(p, "select count(*) from chat_sessions where id='current-only'") == [(1,)]
  assert query(p, "select text from chat_drafts where draft_key='conflict'") == [('backup chat draft',)]
  assert d.state()['ui_scale']['applied'] == 125
  assert d.state()['appearance']['theme'] == 'light'
  assert d.sh('cat '+d.public+'/same.txt') == 'backup file'
  image = json.loads(query(p, "select image_json from draft_attachments where draft_key='conflict'")[0][0])
  expected = hashlib.sha256((d.restore_fixture.src/'assets/asset_1_1.png').read_bytes()).hexdigest()
  assert d.sh('sha256sum '+image['path']).split()[0] == expected
  asset=query(p, "select stored_path from message_assets where session_id='conflict'")[0][0]
  assert d.sh('sha256sum '+asset).split()[0] == expected
  original_hash = hashlib.sha256((d.restore_fixture.data/'assets/fixture-current.png').read_bytes()).hexdigest()
  assert d.sh('sha256sum '+d.data+'/assets/fixture-current.png').split()[0] == original_hash
  assert json.loads(d.sh('cat '+d.data+'/ime/user.json'))['records'][0]['count'] == 2
  checkpoint('pictures-drafts-ime-theme-scale-and-retained-chat-verified')

  start('mixed')
  conflicts = [u for u in state()['plan']['items'] if u['conflict']]
  assert len(conflicts) == 3
  for index, item in enumerate(conflicts):
    assert state()['conflict'] == index
    control('skip' if item['kind'] == 'provider' else 'replace')
    d.wait(lambda: state()['screen'] == 'summary' or state()['conflict'] == index+1)
  p = apply('individual-mixed-choices')
  assert query(p, "select api_key from providers where id='p'") == [('backup-secret',)]
  assert query(p, "select display_name from model_configs where id='m'") == [('Mixed model',)]
  assert query(p, "select output_text from chat_messages where id='message-conflict'") == [('mixed restored text',)]
  assert query(p, "select text from chat_drafts where draft_key='conflict'") == [('mixed restored draft',)]
  start('mixed')
  bulk('skip')
  _, before_noop = snapshot('before-noop')
  apply('all-skip-no-new-items-does-not-reload', reload=False)
  assert snapshot('after-noop')[1] == before_noop
  checkpoint('no-op-preserves-database-and-native-identity')
  assert d.sh('test ! -e '+d.data+'/.rescue/restore-journal.json && echo clean') == 'clean'
  evidence['passed'] = True
  d.record()
  return evidence


def main():
  p = argparse.ArgumentParser(description=__doc__)
  p.add_argument('--serial', required=True, choices=tuple(r.DEVICES))
  p.add_argument('--package', required=True, type=pathlib.Path)
  p.add_argument('--label', default='devices')
  args = p.parse_args()
  assert args.label.replace('-', '').isalnum()
  r.OUT = r.ROOT/'.work/backup-restore'/args.label
  install_termination_handler()
  d = r.ReleaseDevice(args.serial, args.package, ['backup-restore'], True)
  d.saved = d.data+'.backup-restore-acceptance-saved'
  d.public_saved = d.public+'.backup-restore-acceptance-saved'
  d.result.update(scope='backup-restore-only',paper_scan_quality='out of scope',
                  human_visual_approval='scoped screenshots require assistant inspection',
                  keyboard_brightness_approval='out of scope')
  with __import__('zipfile').ZipFile(args.package) as package:
    d.expected_native = hashlib.sha256(package.read('assets/bin/penchat-lvgl')).hexdigest()
  try:
    d.run()
  finally:
    if hasattr(d, 'restore_fixture'):
      d.restore_fixture.tearDown()


if __name__ == '__main__':
  main()
