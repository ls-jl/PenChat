"""Assistant toolbar and data effects using real mapped touchscreen events."""
import json,sqlite3,subprocess,time,types

def run(env,Server):
    d=types.SimpleNamespace(**env);W,H=d.W,d.H
    def click(b):d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def action(id,key):
        for _ in range(25):
            b=d.state()['reply_actions']['rows'][id][key]
            if 54<=b[1] and b[3]<H-65:click(b);return
            d.settings_swipe(75 if b[1]<54 else -75)
        raise AssertionError('Assistant action never became visible: '+id+' '+key)
    def bottom():
        s=d.state()
        if s['to_bottom_visible']:d.tap(s['to_bottom_x']+22,s['to_bottom_y']+22)
        d.wait(lambda:d.state()['toolbar_y']==H-58)
    def current_db():
        path=d.OUT/'reply-actions-result.db'
        remote='/tmp/penchat-reply-test-snapshot.db'
        subprocess.run(['adb','-s',d.SERIAL,'shell',"umask 077; sqlite3 -readonly "+d.DATA+"/penchat.db '.backup "+remote+"'"],check=True,capture_output=True)
        subprocess.run(['adb','-s',d.SERIAL,'pull',remote,str(path)],check=True,capture_output=True)
        subprocess.run(['adb','-s',d.SERIAL,'shell','rm -f '+remote],check=True,capture_output=True);path.chmod(0o600)
        return sqlite3.connect(path)
    def select(id):
        bottom();d.tap(40,32);d.wait(lambda:d.state()['navigation_open'])
        row=next(r for r in d.state()['history_rows'] if r['id']==id);d.tap(row['x'],row['y']);d.wait(lambda:d.state()['session']==id and not d.state()['navigation_open'])
    action('a2','copy');assert d.state()['editor']['clipboard_count']==1;d.capture('assistant-toolbar')
    bottom();d.tap(W//2,H-34);d.wait(lambda:d.state()['editor']['active'])
    if d.state()['editor']['chinese']:d.key('language')
    for c in 'draft':d.key(c)
    d.tap(W-80,H-30);d.wait(lambda:not d.state()['editor']['active']);assert d.state()['draft_length']==5
    action('a2','retry')
    assert not d.state()['reply_actions']['confirm']
    d.wait(lambda:bool(Server.requests) and not d.state()['generating'] and not d.state()['compressing'],timeout=25)
    body=Server.requests[-1][1];wire=json.dumps(body)
    assert body['model']=='b' and 'CURRENT_SYSTEM_PROMPT' in wire and 'Later question' not in wire and 'FORBIDDEN_FUTURE_SUMMARY' not in wire
    assert wire.count('data:image/png;base64,')==1 and d.state()['draft_length']==5
    with current_db() as db:
        assert not db.execute("select id from chat_sessions where status='archived'").fetchone()
        assert db.execute('select count(*) from reply_results').fetchone()[0]==2
        assert db.execute("select count(*) from chat_messages where session_id='s'").fetchone()[0]==8
    action('a2','branch');d.wait(lambda:d.state()['session']!='s' and not d.state()['reply_actions']['busy'],timeout=20)
    branch=d.state()['session'];assert d.state()['draft_length']==5 and len(Server.requests)==1
    with current_db() as db:
        title=db.execute('select title from chat_sessions where id=?',(branch,)).fetchone()[0];assert title=='编辑测试 · 分支'
        assert db.execute("select count(*) from chat_messages where session_id='s'").fetchone()[0]==7
        assert db.execute('select count(*) from chat_messages where session_id=?',(branch,)).fetchone()[0]==6
        target=db.execute("select id from chat_messages where session_id=? and role='assistant' order by seq desc limit 1",(branch,)).fetchone()[0]
        assert db.execute('pragma foreign_key_check').fetchall()==[]
    d.capture('assistant-branch');select('s');assert d.state()['draft_length']==0
    select(branch);assert d.state()['draft_length']==5
    action(target,'branch');d.wait(lambda:d.state()['session']!=branch and not d.state()['reply_actions']['busy'],timeout=20)
    with current_db() as db:
        title=db.execute('select title from chat_sessions where id=?',(d.state()['session'],)).fetchone()[0];assert title=='编辑测试 · 分支2'
    assert len(Server.requests)==1 and d.state()['draft_length']==5;d.capture('assistant-branch2')
    select('s');assert d.state()['draft_length']==0
    d.result['assistant_copy_retry_branch']=True
    d.result['branch_moves_draft_and_images']=True
