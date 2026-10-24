#!/usr/bin/env python3
"""Actual history rows, title/time priority, static details and clock recording."""
import datetime,http.server,json,os,pathlib,signal,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_reasoning_ui import seed
from settings_ui import locate
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/history-time';OUT.mkdir(parents=True,exist_ok=True)

def seed_history(folder,port):
    seed(folder,port);now=datetime.datetime.now()
    dates={'today':now,'yesterday':now-datetime.timedelta(days=1),
           'year':datetime.datetime(now.year-1,1,2,10,11),'missing':None}
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute("insert into app_settings values('auto_title_enabled','false',1)")
        for index,(key,date) in enumerate(dates.items()):
            stamp=int(date.timestamp()*1000) if date else 0
            title=('这是用于检查静态完整标题和滚动显示的长会话名称'*5)+key
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,'active',?,?,?)",(key,title,stamp,4-index,stamp))
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values(?,?,1,'assistant','complete','text',?,?,'参考')",(key+'-answer',key,stamp,stamp))
    return dates

def shot(a,name):
    path=a.directory/'lvgl-diagnostic.png'
    if path.exists():path.unlink()
    a.process.send_signal(signal.SIGUSR1);t.wait_for(path.exists)
    (OUT/(name+'.png')).write_bytes(path.read_bytes())

def mode(a,key,w,h):
    a.to_chat();a.open_settings();a.setting('history_time');t.wait_for(lambda:a.diag()['modal'])
    shot(a,f'{w}-time-picker')
    index={'time':0,'title':1,'off':2}[key];width=(w-162)//3
    a.tap(84+16+index*(width+12)+width//2,90)
    t.wait_for(lambda:not a.diag()['modal']);a.to_chat();a.open_navigation()

def run(w,h):
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),t.Server) as server,tempfile.TemporaryDirectory(prefix='penchat-history-time-') as directory:
        t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start()
        p=pathlib.Path(directory);dates=seed_history(p,server.server_port)
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        try:
            a.open_navigation();s=a.diag();caps=s['history_captions']
            assert caps['today']['time']==dates['today'].strftime('%H:%M')
            assert caps['yesterday']['time']==dates['yesterday'].strftime('%m月%d日 %H:%M' if dates['yesterday'].year==dates['today'].year else '%Y年%m月%d日 %H:%M')
            assert caps['year']['time']==dates['year'].strftime('%Y年%m月%d日 %H:%M')
            assert caps['missing']['time']==''
            for key in dates:
                assert caps[key]['marquee'] and caps[key]['title_length']>60
                if caps[key]['time_bounds']:assert caps[key]['title_bounds'][2]<caps[key]['time_bounds'][0]
            shot(a,f'{w}-time-first')
            row=next(r for r in s['history_rows'] if r['id']=='today')
            a.long_press(row['x'],row['y']);s=a.diag()
            assert s['history_details']['title_length']==caps['today']['title_length']
            assert str(datetime.datetime.now().year)+'年' in s['history_details']['time']
            actions=s['history_actions'];assert len(actions)==5
            assert len({r[1] for r in actions.values()})==1 and all(r[3]-r[1]+1==50 for r in actions.values())
            shot(a,f'{w}-static-details');a.tap(w-10,h-10)
            mode(a,'title',w,h);s=a.diag()
            assert all(not c['time'] and not c['marquee'] for c in s['history_captions'].values())
            shot(a,f'{w}-title-first')
            mode(a,'off',w,h);assert all(not c['time'] for c in a.diag()['history_captions'].values())
            shot(a,f'{w}-hidden')
            a.to_chat();a.edit('关闭显示时仍记录真实发送与回复完成时间');sent=int(time.time()*1000)
            a.tap(w-52,h-34);t.wait_for(lambda:any(row[0]=='assistant' and row[1]=='complete' for row in a.read_messages()))
            t.wait_for(lambda:not a.diag()['generating'])
            with sqlite3.connect(p/'penchat.db') as db:
                rows=db.execute("select role,metadata_json from chat_messages where session_id=? order by seq",(a.diag()['session'],)).fetchall()
                user=json.loads(next(m for role,m in rows if role=='user'))['timeline']
                assistant=json.loads(rows[-1][1])['timeline']
                assert user['sent_at']>=sent and assistant['completed_at']>=user['sent_at']
                stamp=db.execute('select last_message_at from chat_sessions where id=?',(a.diag()['session'],)).fetchone()[0]
                assert stamp==assistant['completed_at']
                assert db.execute("select last_message_at from chat_sessions where id='missing'").fetchone()[0]==0
            a.send('icons.test');shot(a,f'{w}-icons-fixed')
        finally:a.close();server.shutdown()
        b=t.Native(BINARY,p)
        try:
            b.open_settings();assert b.diag()['settings']['rows']['history_time']
            with sqlite3.connect(p/'penchat.db') as db:assert json.loads(db.execute("select value_json from app_settings where key='history_time_display'").fetchone()[0])=='off'
        finally:b.close()

if __name__=='__main__':
    for size in ((960,266),(936,280)):run(*size)
    print('History time UI: calendar formats, missing dates, priority/marquee, full static title, single-row actions, off-mode recording and restart passed at both sizes.')
