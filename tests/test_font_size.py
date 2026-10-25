#!/usr/bin/env python3
"""Actual LVGL reading scale: layout, slider, cancellation and persistence."""
import http.server, json, os, pathlib, sqlite3, tempfile, threading, time
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
from settings_ui import locate

BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))
OUT = pathlib.Path(os.environ.get('PENCHAT_FONT_ARTIFACTS', t.ROOT/'.work/font-size')); OUT.mkdir(parents=True, exist_ok=True)
t.port = 1
SAMPLE = '# 字体大小 Aa\n\n正文与 English words 混排，字号变化后重新换行。\n\n' \
         '$x^2+1$\n\n| 项目 | 数值 |\n| --- | --- |\n| 宽表格 | $\\frac{1}{2}$ |\n\n' \
         '```cpp\nint answer = 42;\n```\n\n结束。'

def fixture(folder):
    seed(folder, 1)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute("insert into app_settings values('active_session_id','\"fixture\"',1)")
        db.execute("insert into app_settings values('reasoning_display','\"always\"',1)")
        for session, status in [('fixture','active'), ('archive','archived')]:
            db.execute('insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,?,1,1,1)', (session,'字号检查',status))
            for seq,role,text,reason in [(1,'user','字号改变后，气泡内的中文、English words 以及换行应该保持完整。'*3,''),
                                          (2,'assistant',SAMPLE,'这是一段思考过程。缩放时它与正文保持同样的字号。')]:
                db.execute('insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,reasoning_text,created_at,updated_at) values(?,?,?, ?,\'complete\',\'text\',?,?,1,1)',
                           (session+str(seq),session,seq,role,text,reason))

def slider(a, percent, drag=False):
    x1,y1,x2,y2 = a.diag()['reading_font']['slider']
    x = x1 + round((percent-75)/125 * (x2-x1)); y=(y1+y2)//2
    if drag:
        start = x1 + (x2-x1)//2
        for i in range(7):
            a.send('touch', x=round(start+(x-start)*i/6),y=y,pressed=True);time.sleep(.025)
        a.send('touch',x=x,y=y,pressed=False)
    else: a.tap(x,y)
    t.wait_for(lambda:a.diag()['reading_font']['draft']==percent)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-font-') as temp:
        folder=pathlib.Path(temp);fixture(folder)
        a=t.Native(BINARY,folder,screen={'width':w,'height':h,'direction':0})
        heights={}
        try:
            a.edit('字号调整时保留草稿')
            for percent in (75,100,150,200):
                a.open_settings();locate(a.diag,a.settings_swipe,'font_size')
                scroll=a.diag()['scroll_y'];a.setting('font_size')
                assert a.diag()['view']=='font_size'
                slider(a,percent,drag=percent==200)
                capture(a, OUT/f'{w}-preview-{percent}.png')
                assert a.diag()['reading_font']['percent']==(100 if not heights else list(heights)[-1])
                a.page_action('保存');t.wait_for(lambda:a.diag()['view']=='settings')
                assert a.diag()['scroll_y']==scroll
                a.to_chat();state=a.diag()
                assert state['draft_length']==len('字号调整时保留草稿')
                assert state['reading_font']['percent']==percent
                assert state['reading_font']['size']==(21*percent+50)//100
                b=state['message_interactions']['bubbles']['fixture1']
                assert b[2]-b[0] <= (w-112)*3//4
                heights[percent]=b[3]-b[1]+1
                for _ in range(30):
                    if a.diag()['scroll_y']==0:break
                    a.settings_swipe(120)
                capture(a,OUT/f'{w}-chat-{percent}.png')
                # The entire reader remains scrollable at every size.
                for _ in range(20):
                    y=a.diag()['table_rows'][0]['y']
                    if 25<=y<=100:break
                    a.settings_swipe(max(-120,min(120,90-y)))
                capture(a,OUT/f'{w}-rich-{percent}.png')
            assert heights[75]<heights[100]<heights[150]<heights[200],heights
            a.open_settings();a.setting('font_size');slider(a,75)
            a.tap(40,32);assert a.diag()['view']=='settings'
            assert a.diag()['reading_font']['percent']==200
            a.setting('font_size');a.page_action('恢复默认')
            assert a.diag()['reading_font']['draft']==100
            a.tap(40,32);assert a.diag()['reading_font']['percent']==200
        finally:a.close()
        a=t.Native(BINARY,folder,screen={'width':w,'height':h,'direction':0})
        try:
            assert a.diag()['reading_font']['percent']==200
            a.open_settings();a.setting('archives')
            b=a.diag()['archive_views']['archive'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            assert a.diag()['view']=='archive_chat' and a.diag()['reading_font']['size']==42
            capture(a,OUT/f'{w}-archive-200.png')
            a.open_settings();a.setting('font_size');a.page_action('恢复默认');a.page_action('保存')
            assert a.diag()['reading_font']['percent']==100
        finally:a.close()
        for raw,expected in [('null',100),('"large"',100),('false',100),('-50',75),('900',200)]:
            with sqlite3.connect(folder/'penchat.db') as db:
                db.execute("update app_settings set value_json=? where key='reading_font_percent'",(raw,))
            a=t.Native(BINARY,folder,screen={'width':w,'height':h,'direction':0})
            try:assert a.diag()['reading_font']['percent']==expected
            finally:a.close()
    print(f'Font size {w}x{h}: 75–200%, preview, reflow, draft, cancel/reset, archive and restart passed.',flush=True)

def stream():
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),t.Server) as server:
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            for api in ('responses','chat'):
                with tempfile.TemporaryDirectory(prefix='penchat-font-stream-') as temp:
                    folder=pathlib.Path(temp);seed(folder,server.server_port)
                    with sqlite3.connect(folder/'penchat.db') as db:
                        db.execute("update providers set metadata_json=?",(json.dumps({'api':api}),))
                        db.execute("update model_configs set name='scroll' where id='a'")
                        db.execute("insert into app_settings values('reading_font_percent','200',1)")
                        db.execute("insert into app_settings values('auto_title_enabled','false',1)")
                    a=t.Native(BINARY,folder)
                    try:
                        a.edit('流式输出时调整字号');a.tap(908,232)
                        t.wait_for(lambda:a.diag()['generating'])
                        time.sleep(.2);capture(a,OUT/f'stream-{api}-200.png')
                        a.open_settings();a.setting('font_size');slider(a,75)
                        a.page_action('保存');a.to_chat()
                        t.wait_for(lambda:not a.diag()['generating'],timeout=15)
                        assert a.diag()['reading_font']['size']==16
                        t.wait_for(lambda:not a.diag()['storage']['unsaved'])
                        with sqlite3.connect(folder/'penchat.db') as db:
                            content,status=db.execute("select output_text,status from chat_messages where role='assistant'").fetchone()
                        assert t.Server.requests[-1][0].endswith('/chat/completions' if api=='chat' else '/responses')
                        assert '第 29 段' in content and '回复完成。' in content and status=='complete'
                        capture(a,OUT/f'stream-{api}-75.png')
                    finally:a.close()
        finally:server.shutdown()
    print('Font size: Responses and Chat Completions stream reflow without lost content passed.',flush=True)

if __name__=='__main__':
    for size in [(960,266),(936,280)]:run(*size)
    stream()
