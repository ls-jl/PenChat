#!/usr/bin/env python3
"""Completion preserves real bottom following in auto/manual/off without pinning drafts."""
import os,pathlib,tempfile,sqlite3,json,time
import test_lvgl_app as t
from test_chat_style import seed

t.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
for w,h in [(960,266),(936,280),(640,260)]:
 for mode in ['auto','manual','off']:
  with tempfile.TemporaryDirectory(prefix='penchat-completion-mode-') as folder:
   seed(folder,'\n\n'.join('阅读段落 '+str(i) for i in range(100)))
   with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:db.execute('insert into app_settings values(?,?,1)',('landscape_composer_shortcut',json.dumps(mode)))
   a=t.Native(binary,folder,official_adapter=False,screen={'width':w,'height':h})
   try:
    before=a.diag();assert before['at_latest_bottom'] and before['following_latest']
    b=before['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);t.wait_for(lambda:a.diag()['editor']['active'])
    a.editor_action('official');r=a.receive('keyboard.open');a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text='长文 English。'*120)
    t.wait_for(lambda:not a.diag()['editor']['active']);time.sleep(.4);d=a.diag()
    assert not d['composer_hidden'] and d['at_latest_bottom'] and d['following_latest'],(w,mode,d['at_latest_bottom'],d['following_latest'])
    for i in range(9):a.send('touch',x=w//2,y=60+10*i,pressed=True);time.sleep(.025)
    a.send('touch',x=w//2,y=140,pressed=False);time.sleep(.4)
    assert a.diag()['composer_hidden'],(w,mode,'draft became permanently pinned')
    print('LANDSCAPE_COMPLETION_RETAINS_BOTTOM_AND_READER_HIDE',w,mode,flush=True)
   finally:a.close()
