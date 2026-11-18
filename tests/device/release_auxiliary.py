"""Auxiliary business loops inside release_acceptance's disposable outer scope.

Importing this module performs no device work. Only the device owner calls these
entry points; they never read the saved original directory or public user files.
"""
import contextlib
import http.server
import json
import pathlib
import sqlite3
import re
import socket
import sys
import tempfile
import threading
import time

from release_lifecycle import pull_database

ROOT = pathlib.Path(__file__).resolve().parents[2]
if str(ROOT/'tests') not in sys.path:
  sys.path.insert(0, str(ROOT/'tests'))


@contextlib.contextmanager
def _server(d, handler):
  from local_http import server_for_device
  server,host=server_for_device(ROOT,d.serial,handler)
  d.auxiliary_fixture_url = 'http://'+host+':'+str(server.server_port)+'/v1'
  try:
    yield server.server_port
  finally:
    server.shutdown()
    server.server_close()
    del d.auxiliary_fixture_url


def _replace(d, fixture):
  assert d.sh('test -d '+d.saved+' && echo saved || true') == 'saved' or not d.had_data
  assert d.data != d.saved
  d.close()
  # Only the test DB is replaced; no user data lies in d.data in this scope.
  d.sh('rm -f '+d.data+'/penchat.db '+d.data+'/penchat.db-wal '+d.data+'/penchat.db-shm '+d.data+'/penchat.db-journal')
  with sqlite3.connect(fixture) as db:
    db.execute('update providers set base_url=?', (d.auxiliary_fixture_url,))
    db.execute('insert or replace into app_settings values(?,?,1)',
      ('onboarding',json.dumps({'version':1,'complete':True,'step':6})))
  d.push(fixture, d.data+'/penchat.db')
  d.launch()


def _rows(d, sql, args=()):
  path = d.out/'auxiliary-fake-snapshot.db'
  pull_database(d.serial, d.data, path, d.sh)
  with sqlite3.connect(path) as db:
    assert not db.execute('pragma foreign_key_check').fetchall()
    return db.execute(sql, args).fetchall()


def _idle(d):
  return d.wait(lambda: not d.state()['generating'] and
    not d.state().get('compressing', False) and
    not d.state().get('storage', {}).get('unsaved', 0), seconds=60)


def _send(d, text):
  d.chat()
  d.tap(d.state()['composer_text_bounds'])
  d.wait(lambda: d.state()['editor']['active'])
  if d.state()['editor']['layout'] != 'en26':
    d.editor_key('language')
  assert d.state()['editor']['layout'] == 'en26'
  for char in text:
    d.editor_key(char)
  d.editor_action('done')
  d.wait(lambda: not d.state()['editor']['active'])
  state = d.state()
  assert state['draft_length'] == len(text)
  d.touch(state['send_center_x2']/2, state['send_center_y2']/2)


def _attachment(d, name):
  d.chat()
  d.tap(d.state()['attachment_button'])
  d.wait(lambda: d.state()['attachments'])
  for _ in range(40):
    state = d.state()
    b, v = state['attachment_actions'][name], state['attachment_viewport']
    if b[1] >= v[1] and b[3] <= v[3]:
      d.tap(b)
      return
    direction = -1 if b[3] > v[3] else 1
    x = (v[0]+v[2])/2
    start = v[1]+(v[3]-v[1])*(.75 if direction < 0 else .25)
    d.drag(x, start, x, start+direction*min(100, (v[3]-v[1])*.5))
  raise AssertionError('Auxiliary attachment action unreachable: '+name)


def run_title(d):
  from test_history_ui import Server
  from test_reasoning_ui import seed
  class TitleServer(Server):
    requests = []
    titles = []
    completed = 0
    title_delay = 0
    fail_title = False
    fail_answer = False
  with _server(d, TitleServer) as port, tempfile.TemporaryDirectory(prefix='penchat-title-fixture-') as temp:
    folder = pathlib.Path(temp)
    seed(folder, port)
    with sqlite3.connect(folder/'penchat.db') as db:
      db.execute('insert into app_settings values(?,?,1)', ('auto_title_enabled', 'true'))
      db.execute('insert into app_settings values(?,?,1)', ('auto_context_compression', 'false'))
      db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at,metadata_json) values('old','Manual retained title','active',1,1,1,?)",
        (json.dumps({'title_state': 'manual'}),))
      db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values('old-answer','old',1,'assistant','complete','text',1,1,'Old fixture answer')")
    _replace(d, folder/'penchat.db')
    assert not TitleServer.titles, 'Existing history was retroactively titled'
    # Use the semantic New chat footer rather than assuming selected startup ID.
    d.chat()
    state=d.state();scale=state['ui_scale']['applied'];dp=lambda value:(value*scale+50)//100
    x=state['screen_safety']['navigation_x'] if d.h>d.w else dp(16)
    y=state['safe_insets'][1] if d.h>d.w else dp(8)
    size=max(44,dp(48))
    d.touch(x+size/2,y+size/2)
    d.wait(lambda: d.state()['navigation_open'])
    d.tap(d.state()['navigation_footer']['newchat']['bounds'])
    _send(d, 'title')
    d.wait(lambda: len(TitleServer.requests) == 1)
    _idle(d)
    sid = d.state()['session']
    def named():
      rows = _rows(d, 'select title,metadata_json from chat_sessions where id=?', (sid,))
      return rows[0] if rows and json.loads(rows[0][1]).get('title_state') == 'complete' else None
    title, metadata = d.wait(named, seconds=25)
    assert len(TitleServer.titles) == 1 and TitleServer.titles[0]['model'] == 'a'
    assert title and '\n' not in title and len(title) <= d.state()['title_limit']
    assert 'effort' not in TitleServer.titles[0].get('reasoning', {})
    _send(d, 'again');_idle(d)
    d.close();d.launch()
    time.sleep(.4)
    assert len(TitleServer.titles) == 1
    assert _rows(d, 'select title from chat_sessions where id=?', (sid,))[0][0] == title
    assert _rows(d, "select title from chat_sessions where id='old'")[0][0] == 'Manual retained title'
    d.capture('auxiliary-title-completed')
    result = {'generated_once_after_answer': True, 'same_chat_model': True,
      'second_turn_and_restart_preserve_title': True, 'old_manual_title_preserved': True,
      'answer_requests': len(TitleServer.requests), 'title_requests': len(TitleServer.titles)}
  (d.out/'auxiliary-title-results.json').write_text(json.dumps(result, indent=2)+'\n')
  return result


def run_compression(d):
  from test_context import Server, fixture
  class ContextServer(Server):
    requests = []
    delay = .2
    fail = False
    verbose = False
  with _server(d, ContextServer) as port, tempfile.TemporaryDirectory(prefix='penchat-context-fixture-') as temp:
    folder = pathlib.Path(temp)
    fixture(folder, port, rounds=40, budget=16384, api='responses', image=False)
    _replace(d, folder/'penchat.db')
    _send(d, 'pending')
    d.wait(lambda: bool(ContextServer.requests), seconds=30)
    _idle(d)
    def checkpoint():
      return json.loads(_rows(d, "select metadata_json from chat_sessions where id='s'")[0][0]).get('context_compression', {})
    cp = checkpoint()
    assert cp.get('summary') and cp.get('archive_id') and 0<cp['through']<80 and cp['through']%2==0 and cp['revision']==1, cp
    calls = [body for _, body in ContextServer.requests]
    compression = [body for body in calls if 'PenChat_CONTEXT_CHECKPOINT_V2' in json.dumps(body)]
    assert len(compression) >= 1
    assert all(body['model'] == 'b' and 'reasoning' not in body and 'reasoning_effort' not in body for body in compression)
    answer = calls[-1]
    assert answer['model'] == 'a' and 'pending' in json.dumps(answer)
    assert 'opaque-covered' not in json.dumps(compression)
    # The current compactor preserves selected original user text. Covered
    # source text need not disappear, but opaque state must never be quoted.
    for retained in cp.get('retained',[]):assert retained['text'] in json.dumps(answer)
    assert answer['reasoning']['effort'] == 'high'
    assert _rows(d, 'select count(*) from chat_messages where session_id=?', (cp['archive_id'],))[0][0] == 80
    assert _rows(d, "select count(*) from chat_sessions where status='archived'")[0][0] == 1
    _attachment(d, 'summary')
    d.wait(lambda: d.state()['view'] == 'context_summary')
    d.capture('auxiliary-compression-summary')
    d.chat()
    _attachment(d, 'compress')
    d.wait(lambda: len(ContextServer.requests) > len(calls), seconds=30)
    _idle(d)
    assert checkpoint()['revision'] == 2
    assert _rows(d, "select count(*) from chat_sessions where status='archived'")[0][0] == 2
    d.close();d.launch()
    assert checkpoint()['revision'] == 2
    result = {'automatic_and_manual_compression': True, 'archive_message_count': 80,
      'automatic_through': cp['through'], 'revision_after_manual_and_restart': 2,
      'compression_model': 'b', 'answer_model': 'a', 'opaque_covered_items_excluded': True,
      'scope': 'Responses fake local transport; image coverage remains separate'}
  (d.out/'auxiliary-compression-results.json').write_text(json.dumps(result, indent=2)+'\n')
  return result
