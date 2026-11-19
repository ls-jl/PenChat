#!/usr/bin/env python3
"""Single-character learning through real editor candidates and private persistence."""
import json,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
import test_pinyin_ui as p

t.port=1
OUT=t.ROOT/'.work/pinyin-single-learning';OUT.mkdir(parents=True,exist_ok=True)

def run(width,height,layout):
  with tempfile.TemporaryDirectory(prefix='penchat-single-learning-') as directory:
    root=pathlib.Path(directory);profile=root/'ime/user.json'
    def saved():return json.loads(profile.read_text())['records']
    def draft():
      with sqlite3.connect(root/'penchat.db') as db:
        row=db.execute("select text from chat_drafts where draft_key='new'").fetchone()
        return row[0] if row else ''
    def choose(app,index):
      e=p.editor(app);name='choose:'+str(e['revision'])+':'+str(index)
      b=e['actions'][name];v=e['candidate_bounds']
      if b[0]<v[0] or b[2]>v[2] or b[1]<v[1] or b[3]>v[3]:
        app.editor_action('expand');t.wait_for(lambda:p.editor(app)['expanded'])
        e=p.editor(app);name='choose:'+str(e['revision'])+':'+str(index)
        b=e['actions'][name];v=e['candidate_bounds'];assert b[0]>=v[0] and b[2]<=v[2] and b[1]>=v[1] and b[3]<=v[3]
      app.editor_action(name)
      p.selected(app)
    def compose(app,index):
      t.wait_for(lambda:not app.diag()['surface_motion'])
      b=app.diag()['composer_text_bounds'];app.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
      t.wait_for(lambda:p.editor(app)['active'] and not app.diag()['surface_motion'])
      if p.editor(app)['layout']!=layout:p.layout(app,layout)
      for value in ('shi' if layout=='zh26' else '744'):p.key(app,value if layout=='zh26' else 't9:'+value)
      p.ready(app);choose(app,index)
    def finish(app):
      app.editor_action('done');t.wait_for(lambda:not p.editor(app)['active'])
      t.wait_for(profile.exists)
    screen={'width':width,'height':height,'direction':0}
    a=t.Native(p.BINARY,root,official_adapter=False,screen=screen)
    try:
      # In the pinned dictionary, 试 is the seventh normal shi/744 candidate.
      compose(a,6);assert not profile.exists(),'Candidate selection alone must not write learning'
      finish(a);t.wait_for(lambda:any(r['word']=='试' and r['pinyin']=='shi' for r in saved()))
      t.wait_for(lambda:draft()=='试')
      compose(a,0);finish(a);t.wait_for(lambda:draft()=='试试')
      t.wait_for(lambda:next(r for r in saved() if r['word']=='试')['count']==2)
    finally:a.close()
    a=t.Native(p.BINARY,root,official_adapter=False,screen=screen)
    try:
      compose(a,0);finish(a);t.wait_for(lambda:draft()=='试试试')
      t.wait_for(lambda:next(r for r in saved() if r['word']=='试')['count']==3)
      before=profile.read_bytes()
      compose(a,1)
      if 'cancel' in p.editor(a)['actions']:a.editor_action('cancel')
      else:
        for i in range(9):a.send('touch',x=12+18*i,y=400,pressed=True);time.sleep(.025)
        a.send('touch',x=156,y=400,pressed=False)
      t.wait_for(lambda:not p.editor(a)['active'])
      assert profile.read_bytes()==before,'Cancel must not learn a different character'
      a.open_settings();a.setting('keyboard');row=a.diag()['settings']['rows']['pinyin_learning']
      a.tap((row['bounds'][0]+row['bounds'][2])//2,(row['bounds'][1]+row['bounds'][3])//2)
      t.wait_for(lambda:not a.diag()['settings']['rows']['pinyin_learning']['checked']);a.to_chat()
      compose(a,1);a.editor_action('done');t.wait_for(lambda:not p.editor(a)['active'])
      assert profile.read_bytes()==before,'Disabled learning must retain the existing profile unchanged'
    finally:a.close()
    assert profile.read_bytes()==before,'Shutdown must not flush cancelled or disabled choices'
  result=dict(width=width,height=height,layout=layout,nonfirst_single='试',selected_only_not_saved=True,completed_saved=True,reopened_first_choice=True,restarted_first_choice=True,confirmed_count=3,cancel_not_learned=True,disabled_not_learned=True)
  (OUT/f'ui-{width}-{layout}.json').write_text(json.dumps(result,ensure_ascii=False,indent=2));print(result,flush=True)

if __name__=='__main__':
  for size in ((960,266),(640,260),(568,1210)):
    for mode in ('zh26','zh9'):run(*size,mode)
