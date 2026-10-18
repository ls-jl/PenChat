#!/usr/bin/env python3
"""Real-process coverage for the themed composer and pull-to-create gesture."""
import http.server, json, os, pathlib, signal, sqlite3, tempfile, threading, time
from test_lvgl_app import Native, ROOT, wait_for
import test_lvgl_app as transport


def snapshot(app, name):
    app.process.send_signal(signal.SIGUSR1)
    time.sleep(.15)
    output = ROOT / '.work/chatgpt-style'
    output.mkdir(parents=True, exist_ok=True)
    (output / (name + '.png')).write_bytes((app.directory / 'lvgl-diagnostic.png').read_bytes())


def drag(app, ys, release=True, x=500):
    for y in ys:
        app.send('touch', x=x, y=y, pressed=True)
        time.sleep(.025)
    if release:
        app.send('touch', x=x, y=ys[-1], pressed=False)
        time.sleep(.2)


def seed(directory, text, count=1):
    schema = (ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
    with sqlite3.connect(pathlib.Path(directory)/'penchat.db') as db:
        db.executescript(schema)
        db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('fixture','Gesture fixture','active',1,1,1)")
        for i in range(count):
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'fixture',?,'assistant','complete','text',?,1,1)", ('reply'+str(i),i+1,text))
        db.execute("insert into app_settings(key,value_json,updated_at) values('active_session_id','\"fixture\"',1)")


def run():
    transport.port = 1
    binary = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', str(ROOT/'build/host/penchat-lvgl')))
    with tempfile.TemporaryDirectory(prefix='penchat-style-') as directory:
        app = Native(binary, pathlib.Path(directory))
        try:
            initial=app.diag()
            first = initial['session']
            assert initial['send_center_x2']==initial['capsule_endcap_x2']
            assert initial['send_center_y2']==initial['capsule_center_y2']
            app.tap(908,232)  # empty composer must not start a request
            assert not app.diag()['generating'] and not app.read_messages()
            assert app.diag()['summary']['sessions']==0
            snapshot(app,'empty')
            # Timing-sensitive device probes must not encode PNGs on the UI thread.
            image_time=(app.directory/'lvgl-diagnostic.png').stat().st_mtime_ns
            old_time=json.loads((app.directory/'lvgl-diagnostic.json').read_text())['diagnostic_time']
            app.process.send_signal(signal.SIGUSR2)
            wait_for(lambda:json.loads((app.directory/'lvgl-diagnostic.json').read_text())['diagnostic_time']>old_time)
            assert (app.directory/'lvgl-diagnostic.png').stat().st_mtime_ns==image_time
            app.tap(128,232);assert app.diag()['attachments'];snapshot(app,'attachments')
            app.tap(600,70);assert not app.diag()['attachments']
            app.tap(128,232)
            drag(app,[190,170,140])
            assert not app.diag()['attachments'] and not app.diag()['pull_active']
            app.tap(128,232);app.tap(200,38);assert app.diag()['view']=='camera';snapshot(app,'camera')
            app.tap(40,44)
            app.tap(128,232);app.tap(200,82);assert app.diag()['view']=='album';snapshot(app,'album')
            app.tap(40,44)
            # Empty conversations, including unsent drafts, never enter pull-to-create.
            drag(app,[190,170,140,110,80,50,38],False)
            assert not app.diag()['pull_active'] and app.diag()['pull_progress']==0
            app.send('touch',x=500,y=38,pressed=False)
            assert app.diag()['session']==first
            app.edit('草稿内容保持，' * 50)
            drag(app,[190,160,130,90,145])
            app.tap(500,232);request=app.receive('keyboard.open')
            assert request['text']=='草稿内容保持，' * 50
            app.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='')
            wait_for(lambda:not app.diag()['keyboard'])
            snapshot(app,'long-draft')
            app.open_navigation();snapshot(app,'history')
            app.tap(630,30);assert app.diag()['navigation_open']
            drag(app,[190,160,130,90],x=200);assert app.diag()['navigation_open']
            app.tap(180,236);assert app.diag()['summary']['sessions']==0
            app.open_settings();snapshot(app,'settings');app.tap(180,80);snapshot(app,'providers')
            app.tap(790,30);snapshot(app,'provider-form')
            assert app.diag()['navigation_button']=='back'
            app.tap(40,32);assert app.diag()['view']=='provider'
            app.tap(40,32);assert app.diag()['view']=='settings'
            app.tap(40,32);assert app.diag()['navigation_open']
            app.tap(700,80);assert not app.diag()['navigation_open']
            assert app.diag()['navigation_button']=='menu'
            assert app.diag()['summary']['sessions']==0
            app.edit('  \n\t ');app.tap(908,232)
            assert app.diag()['summary']['sessions']==0 and not app.diag()['generating']
            app.edit('有内容才保存');app.tap(908,232)
            wait_for(lambda:any(row[0]=='assistant' and row[1]=='error' for row in app.read_messages()))
            wait_for(lambda:app.diag()['modal']);app.tap(889,220)
            assert app.diag()['summary']['sessions']==1
            app.open_navigation();state=app.diag()
            app.tap(state['navigation_active_x'],state['navigation_active_y'])
            assert app.diag()['view']=='chat' and not app.diag()['navigation_open']
            app.open_navigation();active=app.diag()
            app.long_press(active['navigation_active_x'],active['navigation_active_y'])
            assert app.diag()['history_context']==active['session']
            app.tap(462,172)
            deleted=app.diag()
            assert deleted['navigation_open'] and deleted['session']!=active['session']
            assert deleted['summary']['sessions']==active['summary']['sessions']-1
            app.tap(700,80);assert not app.diag()['navigation_open']
        finally:app.close()
        reopened=Native(binary,pathlib.Path(directory))
        try:assert reopened.diag()['summary']['sessions']==0
        finally:reopened.close()
    with tempfile.TemporaryDirectory(prefix='penchat-style-gestures-') as directory:
        seed(directory,'有内容的会话才能上拉新建。')
        app=Native(binary,pathlib.Path(directory))
        try:
            first=app.diag()['session']
            # The old 92px stroke is below the raised threshold.
            drag(app,[190,170,140,110,98]);assert app.diag()['session']==first
            # 85% of 152px rounds to 129px; 128px stays partial.
            drag(app,[190,170,140,110,80,62],False)
            assert app.diag()['pull_progress']==99
            assert app.diag()['pull_full_distance']==129
            assert app.diag()['pull_reader_height']==208
            drag(app,[61],False)
            assert app.diag()['pull_progress']==100
            assert app.diag()['pull_indicator_y']==168 and app.diag()['pull_content_y']==-110
            snapshot(app,'pull-full')
            drag(app,[140],False);assert 0<app.diag()['pull_progress']<100
            assert app.diag()['pull_content_y']==-38
            snapshot(app,'pull-partial')
            drag(app,[140]);assert app.diag()['session']==first
            assert app.diag()['pull_content_y']==0
            drag(app,[190,170,140],False)
            app.send('touch',x=500,y=140,pressed=False);time.sleep(.025)
            origin=app.diag()['pull_content_y']
            app.send('touch',x=500,y=190,pressed=True);time.sleep(.015)
            before=app.diag()['pull_content_y']
            app.send('touch',x=500,y=175,pressed=True)
            resumed=app.diag()['pull_content_y']
            assert origin<=before<=0 and resumed<=before
            app.send('touch',x=500,y=175,pressed=False);time.sleep(.25)
            assert app.diag()['pull_content_y']==0 and app.diag()['session']==first
            drag(app,[190,184]);assert app.diag()['session']==first
            drag(app,[190,220]);assert app.diag()['session']==first
            # Full release creates once, repeated release cannot create again.
            drag(app,[190,170,140,110,80,50,38]);second=app.diag()['session'];assert second!=first
            app.send('touch',x=500,y=90,pressed=False)
            assert app.diag()['summary']['sessions']==1
            drag(app,[190,170,140,110,80,50,38])
            assert app.diag()['session']==second and not app.diag()['pull_active']
            app.open_navigation();app.tap(200,30);second=app.diag()['session']
            assert second==first
            # System pointer cancel and background transitions never commit a full ring.
            for kind in ['cancel','background']:
                drag(app,[190,170,140,110,80,50,38],False)
                assert app.diag()['pull_progress']==100
                if kind=='cancel':app.send('touch',x=500,y=90,pressed=False,cancelled=True)
                else:
                    app.send('visibility',visible=False);app.send('visibility',visible=True)
                assert not app.diag()['pull_active'] and app.diag()['session']==second
        finally:app.close()
    with tempfile.TemporaryDirectory(prefix='penchat-style-scroll-') as directory:
        # A wide final code block remains horizontally scrollable and excludes the pull gesture.
        seed(directory, '```cpp\n' + 'wide_'*150 + '\n```')
        app=Native(binary,pathlib.Path(directory))
        try:
            original=app.diag()['session']
            drag(app,[65,45,20],False,x=500)
            assert not app.diag()['pull_active']
            app.send('touch',x=300,y=20,pressed=True);app.send('touch',x=300,y=20,pressed=False)
            assert app.diag()['session']==original
            snapshot(app,'wide-code')
        finally:app.close()
    with tempfile.TemporaryDirectory(prefix='penchat-style-pages-') as directory:
        seed(directory,'分页及上推手势测试。',40)
        app=Native(binary,pathlib.Path(directory))
        try:
            session=app.diag()['session']
            initial_scroll=app.diag()['scroll_y']
            for y in [190,186,180,175,170,160,150,140,130,120,110,100,90,80,100,120,140,150]:
                app.send('touch',x=500,y=y,pressed=True);time.sleep(.04)
                sample=app.diag()
                assert sample['scroll_y']==initial_scroll and sample['scroll_bottom']==0
            # Holding a partially retracted pull must not cause elastic scrolling.
            held_offset=app.diag()['pull_content_y']
            for _ in range(3):
                time.sleep(.04);sample=app.diag()
                assert sample['scroll_y']==initial_scroll and sample['pull_content_y']==held_offset
            app.send('touch',x=500,y=150,pressed=False);time.sleep(.25)
            assert app.diag()['pull_content_y']==0 and app.diag()['scroll_y']==initial_scroll
            drag(app,[45,75,110,150,190,230]);time.sleep(.6)
            assert not app.diag()['following_latest']
            # Reaching the bottom in a regular scroll never commits a new session.
            for _ in range(8):
                drag(app,[190,160,130,100,70,40,15]);time.sleep(.1)
                assert app.diag()['session']==session and not app.diag()['pull_active']
                if app.diag()['following_latest']:break
            assert app.diag()['following_latest']
            # Rapid guarded swipes stretch/retract without changing scroll bounds.
            assert app.diag()['pull_guard_remaining_ms']>0
            stable_bottom=app.diag()['scroll_y']
            assert app.diag()['pull_pause_ms']==500
            for _ in range(3):
                previous=None
                for y in [190,170,140,110,80,110,140,110,80,50,38]:
                    app.send('touch',x=500,y=y,pressed=True);time.sleep(.025)
                    guarded=app.diag()
                    assert guarded['scroll_y']==stable_bottom and guarded['scroll_bottom']==0
                    assert not guarded['pull_create_allowed'] and guarded['pull_progress']==0
                    if previous and guarded['pull_active']:
                        last_y,last_offset=previous
                        if y<last_y:assert guarded['pull_content_y']<=last_offset
                        else:assert guarded['pull_content_y']>=last_offset
                    previous=(y,guarded['pull_content_y'])
                assert app.diag()['pull_content_y']<0
                app.send('touch',x=500,y=38,pressed=False);time.sleep(.08)
                assert app.diag()['session']==session and not app.diag()['pull_active']
                assert app.diag()['pull_guard_remaining_ms']>0
            # Holding a fast pull beyond the quiet interval cannot qualify it.
            drag(app,[190,170,140,110,80,50,38],False)
            time.sleep(.6)
            assert not app.diag()['pull_create_allowed'] and app.diag()['pull_progress']==0
            app.send('touch',x=500,y=38,pressed=False);time.sleep(.02)
            assert app.diag()['session']==session and app.diag()['pull_guard_remaining_ms']>0
            # Slow deliberate movement bypasses the guard before a quiet gap.
            for y in range(190,119,-10):
                app.send('touch',x=500,y=y,pressed=True);time.sleep(.065)
            slow=app.diag()
            assert slow['pull_create_allowed'] and 0<slow['pull_progress']<100
            assert slow['pull_guard_remaining_ms']==0 and slow['scroll_y']==stable_bottom
            snapshot(app,'slow-pull')
            app.send('touch',x=500,y=120,pressed=False);time.sleep(.025)
            assert app.diag()['session']==session and app.diag()['pull_guard_remaining_ms']==0
            # Once progress was visible, release/cancellation adds no 500ms wait.
            drag(app,[190,170,140,110,80,50,38],False)
            assert app.diag()['pull_progress']==100 and app.diag()['pull_guard_remaining_ms']==0
            app.send('touch',x=500,y=38,pressed=False);time.sleep(.2)
            assert app.diag()['session']!=session
            # Slow movement delivered in batches still qualifies (Falcon/DRM).
            app.open_navigation();app.tap(200,30)
            drag(app,[45,75,110,150,190,230]);time.sleep(.35)
            app.tap(521,174)
            app.send('touch',x=500,y=190,pressed=True);time.sleep(.02)
            for batch in [[180,170,160],[150,140,130],[120,110,100]]:
                for y in batch:app.send('touch',x=500,y=y,pressed=True)
                time.sleep(.22)
            batched=app.diag()
            assert batched['pull_create_allowed'] and 0<batched['pull_progress']<100
            assert batched['pull_guard_remaining_ms']==0
            app.send('touch',x=500,y=100,pressed=False);time.sleep(.02)
            drag(app,[190,170,140,110,80,50,38])
            assert app.diag()['session']!=session
            # A quiet 500ms also permits a fast full pull after returning to bottom.
            app.open_navigation();app.tap(200,30)
            drag(app,[45,75,110,150,190,230]);time.sleep(.35)
            app.tap(521,174)
            assert app.diag()['pull_guard_remaining_ms']>0
            wait_for(lambda:app.diag()['pull_guard_remaining_ms']==0,timeout=2)
            drag(app,[190,170,140,110,80,50,38])
            assert app.diag()['session']!=session
            # Entering a saved chat at its bottom bypasses that guard.
            app.open_navigation();app.tap(200,30)
            assert app.diag()['session']==session and app.diag()['pull_guard_remaining_ms']==0
            drag(app,[190,170,140,110,80,50,38])
            assert app.diag()['session']!=session
            app.open_navigation();app.tap(200,30)
            assert app.diag()['session']==session
            for _ in range(20):
                drag(app,[45,75,110,150,190,230]);time.sleep(.35)
                if app.diag()['scroll_y']==0:break
            assert app.diag()['scroll_y']==0
            app.tap(500,17);time.sleep(.2)
            older=app.diag()
            assert older['scroll_y']>0 and older['scroll_bottom']==0
            assert older['to_bottom_visible'] and not older['following_latest']
            app.tap(521,174);time.sleep(.2)
            assert app.diag()['following_latest'] and app.diag()['session']==session
        finally:app.close()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),transport.Server) as server:
        transport.port=server.server_port
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        with tempfile.TemporaryDirectory(prefix='penchat-style-cancel-') as directory:
            app=Native(binary,pathlib.Path(directory),'cancel')
            try:
                app.edit('新建时保留已收到的回复');app.tap(908,232)
                wait_for(lambda:any(row[0]=='assistant' and row[2] for row in app.read_messages()))
                drag(app,[190,170,140,110,80,50,38])
                assert app.diag()['summary']['sessions']==1 and not app.diag()['generating']
                replies=[row for row in app.read_messages() if row[0]=='assistant']
                assert replies[-1][1]=='cancelled' and replies[-1][2]
            finally:app.close()
        server.shutdown()
    print('ChatGPT-style UI: attachments, reversible pull progress, commit once, cancel, background, draft retention, history creation and wide-code exclusion passed.')


if __name__=='__main__':run()
