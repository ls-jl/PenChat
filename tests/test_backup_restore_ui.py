#!/usr/bin/env python3
"""Actual native Settings/restore selection, all-conflict choices and reload handoff."""
import json,os,pathlib,signal,sqlite3,time
import test_lvgl_app as t
from test_backup_restore import Restore,sql,OUT

def button(app,key):
 for _ in range(55):
  d=app.diag();b=d['backup_restore']['controls'].get(key);v=d['page']['viewport']
  if not b or d['page']['scrolling']:time.sleep(.07);continue
  if b[1]>=v[1] and b[3]<=v[3]:
   assert b[3]-b[1]+1>=44,(key,b);app.tap((b[0]+b[2])//2,(b[1]+b[3])//2);return
  app.settings_swipe(100 if b[1]<v[1] else -100);time.sleep(.15)
 raise AssertionError((key,app.diag()))
def capture(app,path):
 p=app.directory/'lvgl-diagnostic.png';p.unlink(missing_ok=True);app.process.send_signal(signal.SIGUSR1);t.wait_for(lambda:p.exists() and p.read_bytes().endswith(b'IEND\xaeB`\x82'));path.write_bytes(p.read_bytes())
def run():
 t.port=1;keys=['PENCHAT_TEST_UPDATE_CONFIG','PENCHAT_TRANSFER_ROOT','PENCHAT_TEST_Y07_ROUNDED'];old={k:os.environ.get(k) for k in keys};results=[]
 try:
  for w,h in [(960,266),(640,260),(568,1210)]:
   for scale in [100,150]:
    for theme in ['dark','light']:
     fixture=Restore();fixture.setUp();app=None
     try:
      data=fixture.data;pub=fixture.pub
      config=fixture.root/'updates.json';config.write_text(json.dumps({'root':'http://127.0.0.1:1/penchat-rk/','auto':False,'nodes':[]}));os.environ['PENCHAT_TEST_UPDATE_CONFIG']=str(config);os.environ['PENCHAT_TRANSFER_ROOT']=str(pub);os.environ['PENCHAT_TEST_Y07_ROUNDED']='1' if h>w else '0'
      sql(data,"update chat_sessions set metadata_json='{}'");sql(data,"insert or replace into app_settings values('auto_title_enabled','false',1)");sql(data,"insert or replace into app_settings values('theme_mode',?,1)",(json.dumps(theme),))
      app=t.Native(t.ROOT/'build/host/penchat-lvgl',data,seed_model=False,screen={'width':w,'height':h},ui_scale_percent=scale)
      app.open_settings();assert 'backup_restore' in app.diag()['settings']['rows'];app.setting('backup_restore')
      t.wait_for(lambda:app.diag()['backup_restore']['screen']=='list')
      button(app,'backup:'+fixture.id);t.wait_for(lambda:app.diag()['backup_restore']['screen'] in ['conflict','summary'])
      d=app.diag()['backup_restore'];assert d['screen']=='conflict',d
      name=f'{w}-{scale}-{theme}';capture(app,OUT/(name+'-conflict.png'))
      # Cancel before commit must preserve the current rows.
      if w==960 and scale==100 and theme=='dark':
       app.page_action('取消');t.wait_for(lambda:app.diag()['view']=='settings');assert sql(data,"select output_text from chat_messages where id='message-conflict'")[0][0]=='current text'
       app.setting('backup_restore');t.wait_for(lambda:app.diag()['backup_restore']['screen']=='list');button(app,'backup:'+fixture.id);t.wait_for(lambda:app.diag()['backup_restore']['screen']=='conflict')
       button(app,'replace');t.wait_for(lambda:app.diag()['backup_restore']['conflict']==1)
      action='skip' if theme=='dark' else 'replace'
      button(app,'all');assert app.diag()['backup_restore']['all'];button(app,action)
      try:t.wait_for(lambda:app.diag()['backup_restore']['screen']=='summary')
      except Exception:
       (OUT/'ui-failure-state.json').write_text(json.dumps({'backup':app.diag()['backup_restore'],'page':app.diag()['page']},ensure_ascii=False,indent=2));raise
      d=app.diag()['backup_restore'];assert set(d['decisions'].values())=={action};assert len(d['decisions'])==d['plan']['conflicts']
      capture(app,OUT/(name+'-summary.png'));button(app,'apply');packet=app.receive('backup.finished');assert packet['ok'],packet;assert packet['result']['added']>0;assert app.process.wait(timeout=15)==0;app.close();app=None
      assert sql(data,"select count(*) from chat_sessions where id='current-only'")[0][0]==1
      assert sql(data,"select count(*) from chat_sessions where id='source-new'")[0][0]==1
      expected='current text' if action=='skip' else 'backup text';assert sql(data,"select output_text from chat_messages where id='message-conflict'")[0][0]==expected
      app=t.Native(t.ROOT/'build/host/penchat-lvgl',data,seed_model=False,screen={'width':w,'height':h})
      app.send('backup.return',**{k:v for k,v in packet.items() if k not in ['v','type','generation']});t.wait_for(lambda:app.diag()['view']=='backup_restore' and app.diag()['backup_restore']['screen']=='list')
      assert '完成' in app.diag()['backup_restore']['message'];assert app.diag()['ui_scale']['applied']==(100 if action=='skip' else 125)
      results.append(dict(width=w,height=h,scale=scale,theme=theme,action=action,passed=True))
     finally:
      if app:app.close()
      fixture.tearDown()
 finally:
  for k,v in old.items():
   if v is None:os.environ.pop(k,None)
   else:os.environ[k]=v
 (OUT/'ui-results.json').write_text(json.dumps(results,indent=2)+'\n');print('BACKUP_RESTORE_UI_MATRIX_OK',len(results))
if __name__=='__main__':run()
