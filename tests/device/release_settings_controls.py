"""Persist actual settings changes under the release runner's fake fixture."""
import json
import sqlite3

from release_lifecycle import pull_database


def run(d):
  d.chat();d.settings();results={'toggles':{},'choices':{},'numeric':{}}
  def row(name):
    return d.reveal(lambda s:s['settings']['rows'][name]['bounds'])
  def choice(name,value):
    d.tap(row(name));d.wait(lambda:bool(d.state()['context_menu']['choices']))
    choices=d.state()['context_menu']['choices'];assert value in choices,(name,value,list(choices))
    d.tap(choices[value]['bounds']);d.wait(lambda:not d.state()['modal'])
  for name in ('auto_title','auto_context','image_auto_send','retry_archive_prompt','persistent_exports'):
    old=d.state()['settings']['rows'][name]['checked'];assert isinstance(old,bool)
    d.tap(row(name));d.wait(lambda:d.state()['settings']['rows'][name]['checked'] is not old)
    results['toggles'][name]={'before':old,'saved':not old}
  for name,values in [('web_policy',('on','recent','off')),('reasoning',('collapsed','during','always')),
      ('history_time',('time','title','off')),('message_edit',('ask','archive','overwrite')),
      ('composer_shortcut',('auto','off','manual'))]:
    for value in values:choice(name,value)
    results['choices'][name]={'values_selected':values,'saved':values[-1]}
  def number(value):
    d.wait(lambda:d.state()['editor']['active'])
    for _ in range(d.state()['editor']['length']):d.editor_key('delete')
    e=d.state()['editor']
    if not any(k['key']=='1' for k in e['keys']):d.editor_key('mode')
    for digit in str(value):d.editor_key(digit)
    d.editor_action('done');d.wait(lambda:not d.state()['editor']['active'])
  for value in (10,95):
    d.tap(row('compression_threshold'));number(value)
  results['numeric']['context_compression_percent']=95
  d.tap(row('http_idle'));d.wait(lambda:d.state()['view']=='http_settings')
  for value in (15,600):
    d.tap(d.reveal(lambda s:s['settings']['rows']['http_idle_seconds']['bounds']));number(value)
    assert d.state()['network_transport']['idle_seconds']==value
  results['numeric']['http_idle_seconds']=600
  d.chat();d.close();d.launch()
  assert d.state()['network_transport']['idle_seconds']==600
  snapshot=d.out/'settings-controls-fake.db';pull_database(d.serial,d.data,snapshot,d.sh)
  with sqlite3.connect(snapshot) as db:
    saved={key:json.loads(value) for key,value in db.execute('select key,value_json from app_settings')}
  for name,key in [('auto_title','auto_title_enabled'),('auto_context','auto_context_compression'),
      ('retry_archive_prompt','retry_archive_prompt'),('persistent_exports','persistent_export_directory')]:
    assert saved[key]==results['toggles'][name]['saved'],(key,saved.get(key))
  assert saved['penchat_settings']['autoSendCapturedContent']==results['toggles']['image_auto_send']['saved']
  for name,key in [('web_policy','new_chat_web'),('reasoning','reasoning_display'),('history_time','history_time_display'),
      ('message_edit','message_edit_default'),('composer_shortcut','landscape_composer_shortcut')]:
    assert saved[key]==results['choices'][name]['saved'],(key,saved.get(key))
  for key,value in results['numeric'].items():assert saved[key]==value,(key,saved.get(key))
  d.settings();d.tap(row('about'));d.wait(lambda:d.state()['modal']);d.capture('settings-about-dialog')
  results.update(nondefault_values_survive_native_restart=True,about_dialog_opened=True,
    limitations=['Control persistence is separate from auxiliary title/compression service behavior.',
      'No feedback submission or public update endpoint request is made by this fixture.'])
  (d.out/'settings-controls-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
  return results
