#!/usr/bin/env python3
"""Real touch overscroll for empty, short and long history drawers."""
import pathlib, sqlite3, tempfile, time
from test_lvgl_app import Native, ROOT, wait_for
from test_chat_style import seed
import test_lvgl_app as transport
transport.port=1
for count in (0,1,9):
    with tempfile.TemporaryDirectory(prefix='penchat-bounce-') as directory:
        if count:
            seed(directory,'正文')
            with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
                for i in range(1,count):
                    sid='history'+str(i)
                    db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,'active',1,1,1)",(sid,sid))
                    db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,?,1,'user','complete','text','hello',1,1)",('msg'+str(i),sid))
        app=Native(ROOT/'build/host/penchat-lvgl',pathlib.Path(directory))
        try:
            app.open_navigation();session=app.diag()['session']
            for y in range(70,181,10):
                app.send('touch',x=300,y=y,pressed=True);time.sleep(.025)
            assert app.diag()['navigation_scroll']<0
            app.send('touch',x=300,y=180,pressed=False)
            wait_for(lambda:abs(app.diag()['navigation_scroll'])<=1)
            for _ in range(8 if count>1 else 1):
                for y in range(180,29,-10):
                    app.send('touch',x=300,y=y,pressed=True);time.sleep(.02)
                app.send('touch',x=300,y=30,pressed=False);time.sleep(.4)
            before=app.diag()['navigation_scroll']
            for y in range(180,29,-10):
                app.send('touch',x=300,y=y,pressed=True);time.sleep(.02)
            assert app.diag()['navigation_scroll']>before
            app.send('touch',x=300,y=30,pressed=False)
            wait_for(lambda:app.diag()['navigation_scroll']<=before+1)
            assert app.diag()['navigation_open'] and app.diag()['session']==session
            assert not app.diag()['modal']
        finally:app.close()
print('History: empty/short/long list top and bottom bounce, release settling, no accidental selection passed.')
