#!/usr/bin/env python3
"""Focused actual-LVGL selection gestures; isolated data, no API/device calls."""
import json
import os
import http.server
import pathlib
import signal
import sqlite3
import tempfile
import time
import threading
import queue
import test_lvgl_app as ui
from test_reasoning_ui import seed

OUT=ui.ROOT/'.work/text-selection';OUT.mkdir(parents=True,exist_ok=True)
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',str(ui.ROOT/'build/host/penchat-lvgl')))

def fixture(folder):
    seed(folder,1)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute("INSERT INTO chat_sessions(id,title,status,created_at,updated_at,last_message_at) VALUES('s','Selection fixture','active',1,1,1)")
        for key,value in [('active_session_id','s'),('auto_title_enabled',False)]:
            db.execute('INSERT INTO app_settings VALUES(?,?,1)',(key,json.dumps(value)))
        for seq,mid,role,text in [(1,'u','user','Question word'),(2,'a','assistant','Alpha **bravo** 中文 👩‍💻.')]:
            db.execute("INSERT INTO chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) VALUES(?,'s',?,?,'complete','text',1,1,?)",(mid,seq,role,text))
            db.execute("INSERT INTO message_parts(id,session_id,message_id,part_index,part_type,text) VALUES(?,'s',?,0,'text',?)",(mid,mid,text))

def screenshot(app,name):
    path=app.directory/'lvgl-diagnostic.png'
    if path.exists():path.unlink()
    app.process.send_signal(signal.SIGUSR1);ui.wait_for(path.exists)
    (OUT/(name+'.png')).write_bytes(path.read_bytes())

def bounds_tap(app,b):app.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def press(app,x,y,down=True,**kw):app.send('touch',x=x,y=y,pressed=down,**kw)
def ro(app):return app.diag()['text_selection']
def editor(app):
    state=app.diag()
    return state['editor'] if isinstance(state['editor'],dict) else {'active':False,'ocr_active':False,'selection_only':False,'selected':False,'clipboard_count':state['text_selection']['clipboard_count']}
def clear_selection(app):
    s=app.diag();app.tap(s['width']-4,s['height']//2);time.sleep(.08)

def run(width,height):
    with tempfile.TemporaryDirectory(prefix='penchat-select-') as tmp:
        folder=pathlib.Path(tmp);fixture(folder);ui.port=1
        app=ui.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height,'direction':0})
        try:
            r=next(r for r in ro(app)['regions'] if r['key']=='a')
            b=r['parts'][0];x=b[0]+14;y=b[1]+14
            press(app,x,y);time.sleep(.5)
            assert ro(app)['selected'] and not ro(app)['menu'],ro(app)
            press(app,x,y,False);ui.wait_for(lambda:ro(app)['menu'])
            assert ro(app)['start']==0 and ro(app)['end']==5,ro(app)
            assert not editor(app)['active'] and not editor(app)['ocr_active']
            screenshot(app,str(width)+'-readonly-word')
            m=ro(app)['menu_bounds'];app.tap(m[0]+140,m[1]+25)
            assert editor(app)['clipboard_count']==1
            assert ro(app)['selected']
            m=ro(app)['menu_bounds'];app.tap(m[0]+45,m[1]+25)
            assert ro(app)['end']==len('Alpha bravo 中文 👩‍💻.'),ro(app)
            screenshot(app,str(width)+'-readonly-all')
            clear_selection(app)
            app.tap(x,y);app.tap(x,y);ui.wait_for(lambda:ro(app)['menu'])
            assert ro(app)['start']==0 and ro(app)['end']==5
            # Existing handles can cross; the fixed endpoint remains anchored.
            clear_selection(app)
            app.long_press(x+78,y,.52)
            assert ro(app)['start']==6 and ro(app)['end']==11,ro(app)
            h=ro(app)['handles'][-1];hx=(h[0]+h[2])//2;hy=(h[1]+h[3])//2
            press(app,hx,hy);press(app,x-6,y);press(app,x-6,y,False,cancelled=True);time.sleep(.08)
            assert ro(app)['start']==6 and ro(app)['end']==11 and not ro(app)['menu'],ro(app)
            h=ro(app)['handles'][-1];hx=(h[0]+h[2])//2;hy=(h[1]+h[3])//2
            press(app,hx,hy);press(app,x-6,y);time.sleep(.07);press(app,x-6,y,False)
            assert ro(app)['start']==0 and ro(app)['end']==6,ro(app)
            # Movement before the long hold remains a scroll even after pausing.
            clear_selection(app)
            press(app,x,y);press(app,x,y+20);time.sleep(.52)
            assert not ro(app)['selected'] and not ro(app)['menu'],ro(app)
            press(app,x,y+20,False)
            clear_selection(app)
            press(app,x,y);time.sleep(.5);press(app,x+90,y);time.sleep(.08)
            assert ro(app)['gesture']==5 and ro(app)['end']>5,ro(app)
            before=ro(app)['end'];press(app,x+90,y,False)
            assert ro(app)['menu'] and not editor(app)['active']
            screenshot(app,str(width)+'-readonly-drag')
            # The existing full-reply action still copies original Markdown.
            clear_selection(app)
            bounds_tap(app,app.diag()['reply_actions']['rows']['a']['copy'])
            time.sleep(.1)
            assert ro(app)['clipboard_bytes']==len('Alpha **bravo** 中文 👩‍💻.'.encode())
            clear_selection(app)
            press(app,x,y);time.sleep(.5);press(app,x+80,y);time.sleep(.08)
            press(app,x+80,y,False,cancelled=True);time.sleep(.08)
            assert not ro(app)['selected'] and not ro(app)['menu']
            assert ro(app)['cached_hits']==0
            # User message retains its original business menu and selection page.
            user=app.diag()['message_interactions']['bubbles']['u']
            app.long_press((user[0]+user[2])//2,(user[1]+user[3])//2)
            ui.wait_for(lambda:bool(app.diag()['message_interactions']['actions']))
            actions=app.diag()['message_interactions']['actions'];assert set(actions)=={'copy','select','edit'}
            bounds_tap(app,actions['select']);ui.wait_for(lambda:editor(app)['selection_only'])
            b=editor(app)['text_bounds'];space=editor(app)['coordinate_space']
            tx=b[0]+20;ty=b[1]+17
            if space['scaled']:
                tx=round(tx*space['native_width']/space['width']);ty=round(ty*space['native_height']/space['height'])
            app.long_press(tx,ty,.52)
            assert editor(app)['selected'] and editor(app)['menu'] and not editor(app)['keys'],editor(app)
            screenshot(app,str(width)+'-selection-page')
            before=(editor(app)['selection_start'],editor(app)['selection_end'])
            press(app,tx+150,ty);press(app,tx+150,ty+25);press(app,tx+150,ty+25,False)
            assert (editor(app)['selection_start'],editor(app)['selection_end'])==before,editor(app)
            app.editor_action('cancel');ui.wait_for(lambda:not editor(app)['active'])
            # Editable fields now use the same word gesture with the full menu.
            c=app.diag()['composer_bounds'];bounds_tap(app,c);ui.wait_for(lambda:editor(app)['active'])
            app.editor_action('official');request=app.receive('keyboard.open')
            app.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='Alpha bravo 中文 👩‍💻')
            ui.wait_for(lambda:not editor(app)['active']);bounds_tap(app,app.diag()['composer_bounds']);ui.wait_for(lambda:editor(app)['active'])
            e=editor(app);b=e['text_bounds'];space=e['coordinate_space'];tx=b[0]+22;ty=b[1]+17
            if space['scaled']:
                tx=round(tx*space['native_width']/space['width']);ty=round(ty*space['native_height']/space['height'])
            app.long_press(tx,ty,.52)
            assert editor(app)['selected'] and editor(app)['menu'] and not editor(app)['undo_menu'],editor(app)
            assert all(k in editor(app)['actions'] for k in ['all','copy','paste','cut','undo'])
            screenshot(app,str(width)+'-editable-menu')
            before=(editor(app)['selection_start'],editor(app)['selection_end'])
            press(app,tx+150,ty);press(app,tx+150,ty+25);press(app,tx+150,ty+25,False)
            assert (editor(app)['selection_start'],editor(app)['selection_end'])==before,editor(app)
            # Start away from the selection handles, which intentionally win hit tests.
            app.tap(tx+120,ty);press(app,tx+120,ty);time.sleep(.5);press(app,tx+165,ty);time.sleep(.08)
            assert editor(app)['cursor_drag'] and editor(app)['magnifier'],editor(app)
            press(app,tx+165,ty,False);assert not editor(app)['selected']
            with sqlite3.connect(folder/'penchat.db') as db:
                assert db.execute("SELECT output_text FROM chat_messages WHERE id='a'").fetchone()[0]=='Alpha **bravo** 中文 👩‍💻.'
            print('Unified text selection:',width,height,'passed',flush=True)
        finally:app.close()

def reveal(app,key,link=False):
    for _ in range(25):
        s=app.diag();region=next(r for r in s['text_selection']['regions'] if r['key']==key)
        candidates=region['links'] if link else region['parts']
        for b in candidates:
            if b[1]>=4 and b[3]>b[1] and b[3]<s['reader_height']-8:
                return b
        app.settings_swipe(100);time.sleep(.12)
    raise AssertionError(('Cannot reveal text',key,ro(app)))

def rich_and_lifecycle():
    with tempfile.TemporaryDirectory(prefix='penchat-select-rich-') as tmp:
        folder=pathlib.Path(tmp);fixture(folder)
        with sqlite3.connect(folder/'penchat.db') as db:
            text='Alpha **bravo** [Reference](https://example.org/).\n\n$x^2$\n\n| A | B |\n|---|---|\n| C | D |\n\n```text\na  b\nc\n```'
            db.execute("UPDATE chat_messages SET output_text=?,reasoning_text='Reason **first**.',metadata_json=? WHERE id='a'",(text,json.dumps({'reasoning_observed':True,'reasoning_duration_ms':1300})))
            db.execute("INSERT INTO app_settings VALUES('reasoning_display','\"always\"',1)")
            db.execute("INSERT INTO chat_sessions(id,title,status,created_at,updated_at,last_message_at) VALUES('archive','Archive fixture','archived',1,1,1)")
            for n,role,body in [(1,'user','Archived question'),(2,'assistant','Archived answer')]:
                db.execute("INSERT INTO chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) VALUES(?,'archive',?,?,'complete','text',1,1,?)",('arc'+str(n),n,role,body))
        app=ui.Native(BINARY,folder,official_adapter=False)
        try:
            b=reveal(app,'a',True);x=(b[0]+b[2])//2;y=(b[1]+b[3])//2
            app.tap(x,y);app.tap(x,y);ui.wait_for(lambda:ro(app)['selected'])
            time.sleep(.4);assert not app.diag()['modal'] and ro(app)['end']-ro(app)['start']==9
            screenshot(app,'rich-link-selection');clear_selection(app)
            app.tap(x,y);ui.wait_for(lambda:app.diag()['modal'])
            screenshot(app,'link-single-open');app.tap(889,222);ui.wait_for(lambda:not app.diag()['modal'])
            b=reveal(app,'a:reasoning');app.long_press(b[0]+14,b[1]+14,.52)
            m=ro(app)['menu_bounds'];app.tap(m[0]+45,m[1]+25);m=ro(app)['menu_bounds'];app.tap(m[0]+140,m[1]+25)
            assert ro(app)['clipboard_bytes']==len('Reason first.'.encode())
            screenshot(app,'reasoning-selection')
            app.send('visibility',visible=False);time.sleep(.1);assert not ro(app)['selected']
            app.send('visibility',visible=True);time.sleep(.12)
            b=reveal(app,'a',True);app.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            app.send('visibility',visible=False);time.sleep(.4);app.send('visibility',visible=True);time.sleep(.12)
            assert not app.diag()['modal'] and not ro(app)['link_pending']
            app.open_settings();app.setting('archives');bounds_tap(app,app.diag()['archive_views']['archive'])
            ui.wait_for(lambda:app.diag()['view']=='archive_chat')
            b=reveal(app,'arc2');app.long_press(b[0]+14,b[1]+14,.52)
            assert ro(app)['selected'] and not editor(app)['active']
            screenshot(app,'archive-selection')
            assert not app.diag()['message_interactions']['actions']
            print('Rich text links, reasoning, archive and lifecycle passed',flush=True)
        finally:app.close()

def streaming_selection():
    from test_stream_reading import Stream
    Stream.commands=queue.Queue()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-select-stream-') as tmp:
        threading.Thread(target=server.serve_forever,daemon=True).start();folder=pathlib.Path(tmp);seed(folder,server.server_port);ui.port=server.server_port
        with sqlite3.connect(folder/'penchat.db') as db:
            for k,v in [('auto_title_enabled',False),('auto_context_compression',False),('new_chat_web','off')]:db.execute('INSERT INTO app_settings VALUES(?,?,1)',(k,json.dumps(v)))
        app=ui.Native(BINARY,folder)
        try:
            app.edit('question');app.tap(925,232);ui.wait_for(lambda:app.diag()['generating'])
            Stream.commands.put(('text','Alpha bravo'));ui.wait_for(lambda:app.diag()['output_length']==11)
            ui.wait_for(lambda:any(r['length']==11 for r in ro(app)['regions']))
            region=next(r for r in ro(app)['regions'] if r['length']==11);b=region['parts'][0]
            app.long_press(b[0]+14,b[1]+14,.52);ui.wait_for(lambda:ro(app)['selected'])
            snapshot=ro(app)['active'];Stream.commands.put(('text','\n\n'+'\n\n'.join('More streamed paragraph '+str(n) for n in range(8))))
            ui.wait_for(lambda:app.diag()['output_length']>11);time.sleep(.2)
            assert ro(app)['active']==snapshot and next(r for r in ro(app)['regions'] if r['id']==snapshot)['length']==11
            assert app.diag()['chat_render_pending'] and not app.diag()['following_latest']
            screenshot(app,'stream-selected-snapshot');Stream.commands.put(('done',''))
            ui.wait_for(lambda:not app.diag()['generating'])
            assert ro(app)['selected'] and ro(app)['active']==snapshot
            clear_selection(app);ui.wait_for(lambda:any(r['length']>11 and r['key']==region['key'] for r in ro(app)['regions']))
            assert not app.diag()['following_latest']
            print('Streaming selection snapshot and deferred rendering passed',flush=True)
        finally:Stream.commands.put(('cut',''));app.close();server.shutdown()

if __name__=='__main__':
    for size in [(960,266),(640,360),(568,1210)]:run(*size)
    rich_and_lifecycle()
    streaming_selection()
