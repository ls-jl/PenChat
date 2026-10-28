"""Real keyboard input and persisted prompts, isolated from all user data."""
import sqlite3,subprocess,sys
from http_pool_flow import DeviceServer

def prepare(root,folder,data,serial):
    import shutil
    sys.path.insert(0,str(root/'tests'));from test_reasoning_ui import seed
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir();server=DeviceServer(root,serial,folder.parent);seed(folder,server.server_port)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute('update providers set base_url=?',(f'http://127.0.0.1:{server.server_port}/v1',))
        for key in ['auto_title_enabled','auto_context_compression']:
            db.execute('insert or replace into app_settings values(?,?,1)',(key,'false'))
    return server,server,[(folder/'penchat.db',data+'/penchat.db')]

def run(c,server):
    state,tap,wait=c['state'],c['tap'],c['wait'];W,H=c['W'],c['H']
    def key(k):
        item=next(x for x in state()['editor']['keys'] if x['key']==k);tap(item['x'],item['y'])
    def edit(keys):
        tap(W//2,H-34);wait(lambda:state()['editor']['active'])
        if state()['editor']['layout']!='en26':key('language')
        for k in keys:key(k)
        b=state()['editor']['actions']['done'];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
        wait(lambda:not state()['editor']['active'])
    edit(['enter','enter']);tap(W-52,H-34)
    assert not state()['generating'] and not server.requests
    tap(40,32);wait(lambda:state()['navigation_open'])
    assert not state()['history_rows']
    tap(W-20,80);wait(lambda:not state()['navigation_open'])
    # Reopen the unchanged blank draft; these become the leading blank lines.
    edit(['q','enter','enter']);tap(W-52,H-34)
    wait(lambda:len(server.requests)==1)
    wait(lambda:not state()['generating'] and not state()['storage']['unsaved'])
    assert state()['draft_length']==0
    c['capture']('chat-trimmed-blank-lines');c['close']()
    snapshot=c['OUT']/'whitespace-fixture'/'result.db'
    subprocess.run(['adb','-s',c['SERIAL'],'pull',c['DATA']+'/penchat.db',str(snapshot)],check=True,capture_output=True)
    with sqlite3.connect(snapshot) as db:
        assert db.execute('select count(*) from chat_sessions').fetchone()[0]==1
        assert db.execute("select output_text from chat_messages where role='user'").fetchall()==[('q',)]
        assert db.execute("select p.text from message_parts p join chat_messages m on m.id=p.message_id where m.role='user'").fetchall()==[('q',)]
    c['result']['chat_whitespace']={'blank_has_no_session_or_request':True,'boundary_lines_removed':True,'stored_text_and_parts_match':True,'draft_cleared':True}
