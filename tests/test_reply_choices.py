#!/usr/bin/env python3
"""Actual UI retry candidates, small prompt, request isolation and continuation."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_reply_actions import fixture,Server
from test_ui_theme import App
from image_gestures import capture
OUT=t.ROOT/'.work/reply-choices-20261113/screens';OUT.mkdir(parents=True,exist_ok=True)
class UI(App):
 def settings_swipe(self,delta):
  d=self.diag();top=d['chat_header'].get('bounds') if d['view']=='chat' else None
  y1=top[3]+12 if top else 12;y2=self.h-150 if self.h>self.w else self.h-65
  if self.h>self.w:
   start=min(y2,max(y1+40,int(self.h*.65))) if delta<0 else y1+40;end=max(y1+4,min(y2,start+delta))
  else:start=y2 if delta<0 else max(75,y1+15);end=max(y1+4,min(y2,start+delta))
  for i in range(8):self.send('touch',x=self.w//2,y=start+(end-start)*i//7,pressed=True);time.sleep(.02)
  time.sleep(.2);self.send('touch',x=self.w//2,y=end,pressed=False);time.sleep(.25)
 def reply(self,id,action):
  for _ in range(60):
   d=self.diag();row=d['reply_actions']['rows'].get(id)
   assert row,('missing reply',id,list(d['reply_actions']['rows']))
   b=row[action];top=d['chat_header'].get('bounds');minimum=top[3]+2 if top else 4
   if b and b[1]>=minimum and b[3]<self.h-(145 if self.h>self.w else 58):
    self.tapb(b);time.sleep(.08);return
   self.settings_swipe(150 if b[1]<minimum else -150)
  raise AssertionError(('Unreachable',id,action,self.diag()['scroll_y']))
 def shot(self,name):
  percent=self.diag()['reading_font']['percent'];suffix=f'-{percent}' if percent!=100 else ''
  capture(self,OUT/f'{self.w}{suffix}-{name}.png')
def finished(a):t.wait_for(lambda:not a.diag()['generating'] and not a.diag()['compressing'] and not a.diag()['storage']['unsaved'],20)
def run(w,h,api,percent=100):
 from anthropic_fixture import Handler,configure
 Peer=Handler if api=='anthropic' else Server
 with http.server.ThreadingHTTPServer(('127.0.0.1',0),Peer) as server,tempfile.TemporaryDirectory(prefix='penchat-retry-choice-ui-') as folder:
  t.port=server.server_port;Peer.requests=[];Peer.mode='ok' if api=='anthropic' else 'summary';threading.Thread(target=server.serve_forever,daemon=True).start()
  p=pathlib.Path(folder);fixture(p,server.server_port)
  with sqlite3.connect(p/'penchat.db') as db:
   db.execute("update providers set metadata_json=?",(json.dumps({'api':api}),))
   db.execute("insert or replace into app_settings values('reading_font_percent',?,1)",(json.dumps(percent),))
  if api=='anthropic':
   configure(p,server.server_port)
   with sqlite3.connect(p/'penchat.db') as db:
    meta=json.loads(db.execute("select metadata_json from chat_sessions where id='s'").fetchone()[0]);meta['chat_preferences']['effort']='high';db.execute("update chat_sessions set metadata_json=? where id='s'",(json.dumps(meta),))
  (p/'feedback.json').write_text('{}');(p/'update.json').write_text(json.dumps({'root':'http://127.0.0.1:1/penchat-rk/','nodes':['127.0.0.1'],'auto':False}))
  os.environ.update(PENCHAT_TEST_Y07_ROUNDED='1' if h>w else '0',PENCHAT_TEST_FEEDBACK_CONFIG=str(p/'feedback.json'),PENCHAT_TEST_UPDATE_CONFIG=str(p/'update.json'))
  a=UI(p,w,h)
  try:
   a.reply('a2','retry');t.wait_for(lambda:len(Peer.requests)==1);finished(a)
   d=a.diag();assert d['reply_actions']['rows']['a2']['total']==2 and d['reply_actions']['rows']['a2']['ordinal']==2
   assert 'a3' not in d['reply_actions']['rows'] and not d['modal'];a.shot('second')
   wire=json.dumps(Peer.requests[-1][2 if api=='anthropic' else 1]);assert 'Later question' not in wire and 'FORBIDDEN_FUTURE_SUMMARY' not in wire and '**Old answer**' not in wire
   assert (wire.count('image/png')==1 if api=='anthropic' else wire.count('data:image/png;base64,')==1)
   a.reply('a2','previous');t.wait_for(lambda:not a.diag()['reply_actions']['busy'] and 'a3' in a.diag()['reply_actions']['rows']);a.shot('original-restored')
   a.reply('a2','next');t.wait_for(lambda:not a.diag()['reply_actions']['busy'] and 'a3' not in a.diag()['reply_actions']['rows'])
   a.reply('a2','retry');t.wait_for(lambda:len(Peer.requests)==2);finished(a);assert a.diag()['reply_actions']['rows']['a2']['total']==3
   a.reply('a2','previous');t.wait_for(lambda:not a.diag()['reply_actions']['busy'] and a.diag()['reply_actions']['rows']['a2']['ordinal']==2)
  except BaseException:
   (OUT/f'{w}-failure.json').write_text(json.dumps(a.diag(),ensure_ascii=False,indent=2));a.shot('failure');raise
  finally:a.close()
  a=UI(p,w,h)
  try:
   d=a.diag();assert d['reply_actions']['rows']['a2']['ordinal']==2 and d['reply_actions']['rows']['a2']['total']==3
   a.reply('a2','previous');t.wait_for(lambda:not a.diag()['reply_actions']['busy'] and 'a3' in a.diag()['reply_actions']['rows'])
   a.open_settings();a.setting('retry_archive_prompt');assert a.diag()['settings']['rows']['retry_archive_prompt']['checked'];a.shot('setting');a.to_chat()
   # The latest reply bypasses the prompt even with the switch enabled.
   a.reply('a3','retry');t.wait_for(lambda:len(Peer.requests)==3);finished(a)
   assert not a.diag()['modal'] and a.diag()['reply_actions']['rows']['a3']['total']==2
   a.reply('a2','retry');t.wait_for(lambda:a.diag()['context_menu']['kind']=='retry');a.shot('archive-menu')
   d=a.diag()['context_menu'];b=d['list'];row=64 if h>w else 48;a.tap(b[0]+80,b[1]+row+20)
   t.wait_for(lambda:len(Peer.requests)==4);finished(a)
   with sqlite3.connect(p/'penchat.db') as db:assert not db.execute("select id from chat_sessions where status='archived'").fetchall()
   # Type using the native editor so draft insertion also exercises portrait.
   a.tapb(a.diag()['composer_text_bounds']);t.wait_for(lambda:a.diag()['editor']['active'])
   globe=next(k for k in a.diag()['editor']['keys'] if k['key']=='language');a.long_press(globe['x'],globe['y'],.6);a.editor_action('official');req=a.receive('keyboard.open');a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text='Continue from selected')
   t.wait_for(lambda:not a.diag()['keyboard'] and not a.diag()['editor']['active'])
   d=a.diag();a.tap(d['send_center_x2']//2,d['send_center_y2']//2)
   t.wait_for(lambda:len(Peer.requests)==5);finished(a)
   with sqlite3.connect(p/'penchat.db') as db:
    assert db.execute('select count(*) from reply_groups').fetchone()[0]==0
    assert db.execute("select count(*) from chat_sessions where status='archived'").fetchone()[0]==1
    assert not db.execute("select id from chat_messages where id in ('u3','a3')").fetchall()
    assert not db.execute('pragma foreign_key_check').fetchall()
   a.shot('fixed')
  finally:a.close();server.shutdown()
 print(w,h,api,'direct retry, switch, restore, restart, prompt and atomic continuation passed',flush=True)
if __name__=='__main__':
 for w,h,api in [(960,266,'responses'),(640,260,'chat'),(568,1210,'responses'),(960,266,'anthropic')]:run(w,h,api)
 run(568,1210,'responses',150)
