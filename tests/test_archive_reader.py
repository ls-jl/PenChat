#!/usr/bin/env python3
"""Archived chat is independently readable; browsing never restores or edits it."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_image_viewer import fixture as photo_fixture,shot,Stream

BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))

def fixture(folder,port=1):
    photo_fixture(folder,port=port)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute("update chat_messages set output_text=? where id='reply'",('\n\n'.join('当前聊天阅读位置 '+str(n) for n in range(12)),))
        for id,title,updated in [('archived','归档中的表格与图片',100),('paged','分页归档',90)]:
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,'archived',1,?,1)",(id,title,updated))
        for seq,role,text in [(1,'user','这张图片和表格是什么？'),(2,'user',''),(3,'assistant','归档仍可浏览。\n\n| 项目 | 数值 |\n| --- | --- |\n| 数学 | $x^2+1$ |')]:
            id='archived'+str(seq)
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,reasoning_text,metadata_json,created_at,updated_at) values(?,'archived',?,?,'complete',?,?,?,?,1,1)",(id,seq,role,'image' if seq==2 else 'text',text,'公开思考，查看归档不改变原聊天。' if seq==3 else '',json.dumps({'reasoning_duration_ms':3500} if seq==3 else {})))
            if seq!=2:db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,text) values(?,'archived',?,0,'text',?)",(id+'part',id,text))
        source=db.execute("select stored_path from message_assets where id='asset'").fetchone()[0]
        db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,width,height,created_at) values('archivedAsset','archived','archived2','image','album',?,?,1024,768,1)",(source,source))
        db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('archivedPart','archived','archived2',0,'image','archivedAsset')")
        for n in range(1,66):
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'paged',?,?,'complete','text',?,1,1)",('paged'+str(n),n,'user' if n%2 else 'assistant',f'归档消息 {n}'))
        for n in range(12):
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,'archived',1,?,1)",('extra'+str(n),'另一条归档 '+str(n),n+2))
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,?,1,'assistant','complete','text','归档内容',1,1)",('extra-message'+str(n),'extra'+str(n)))
        db.execute("insert into app_settings values('reasoning_display','\"collapsed\"',1)")

def open_archives(a):
    a.open_settings();a.setting('archives');assert a.diag()['view']=='archives'

def view(a,id):
    b=a.diag()['archive_views'][id];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    t.wait_for(lambda:a.diag()['view']=='archive_chat')
    assert a.diag()['archive_reader']['id']==id

def status(folder,id):
    with sqlite3.connect(folder/'penchat.db') as db:return db.execute('select status from chat_sessions where id=?',(id,)).fetchone()[0]

def run():
    with tempfile.TemporaryDirectory(prefix='penchat-archive-reader-') as directory:
        folder=pathlib.Path(directory);fixture(folder);a=t.Native(BINARY,folder)
        try:
            a.edit('正在聊天的草稿');a.settings_swipe(120);time.sleep(.2);original=a.diag()
            open_archives(a);shot(a,'archive-list-view')
            list_scroll=a.diag()['scroll_y'];view(a,'archived');s=a.diag()
            assert s['session']==original['session'] and s['draft_length']==original['draft_length']
            assert s['active_model']==original['active_model'] and status(folder,'archived')=='archived'
            assert s['archive_reader']['messages']==['archived1','archived2','archived3']
            assert not s['reply_actions']['rows'] and not s['message_interactions']['bubbles']
            shot(a,'archive-reader-top')
            for _ in range(12):
                photo=a.diag()['chat_images'][0]
                if photo['y']>=4 and photo['y']+photo['height']<=262:break
                a.settings_swipe(-60 if photo['y']+photo['height']>262 else 60)
            a.tap(photo['x']+photo['width']//2,photo['y']+photo['height']//2)
            t.wait_for(lambda:a.diag()['image_viewer']['loaded']);shot(a,'archive-reader-photo')
            a.tap(922,30);assert a.diag()['view']=='archive_chat'
            for _ in range(12):
                s=a.diag()
                if 24<=s['reasoning_header_y']<=240:break
                a.settings_swipe(-90 if s['reasoning_header_y']>240 else 90)
            s=a.diag();a.tap(s['reasoning_header_x'],s['reasoning_header_y'])
            assert a.diag()['reasoning_expanded_count']==1;shot(a,'archive-reader-reasoning')
            s=a.diag();a.tap(s['reasoning_header_x'],s['reasoning_header_y'])
            assert a.diag()['reasoning_expanded_count']==0
            a.settings_swipe(-120);assert a.diag()['table_rows'];shot(a,'archive-reader-table')
            a.tap(40,32);assert a.diag()['view']=='archives' and a.diag()['scroll_y']==list_scroll
            # Title tap is also a viewer; a long press remains non-destructive details.
            row=next(r for r in a.diag()['history_rows'] if r['id']=='archived')
            a.long_press(row['x'],row['y']);assert a.diag()['modal'] and a.diag()['view']=='archives'
            a.tap(889,222);assert not a.diag()['modal'];row=next(r for r in a.diag()['history_rows'] if r['id']=='archived')
            a.tap(row['x'],row['y']);assert a.diag()['view']=='archive_chat';a.tap(40,32)
            view(a,'paged');assert a.diag()['archive_reader']['messages']==['paged'+str(n) for n in range(36,66)]
            a.page_action('加载更早消息');assert a.diag()['archive_reader']['messages']==['paged'+str(n) for n in range(6,36)]
            a.page_action('加载更早消息');assert a.diag()['archive_reader']['messages']==['paged'+str(n) for n in range(1,6)]
            shot(a,'archive-reader-pagination');a.page_action('加载较新消息')
            assert a.diag()['archive_reader']['offset']==30
            a.to_chat();s=a.diag();assert s['session']==original['session'] and s['draft_length']==original['draft_length']
            assert s['active_model']==original['active_model'] and status(folder,'archived')=='archived'
            assert s['scroll_y']==original['scroll_y']
            assert status(folder,'paged')=='archived'
            open_archives(a)
            for _ in range(12):
                b=a.diag()['archive_views']['extra0']
                if b[1]>=4 and b[3]<262:break
                a.settings_swipe(-110);time.sleep(.4)
            time.sleep(.4)
            scrolled=a.diag()['scroll_y'];assert scrolled>0
            view(a,'extra0');assert a.diag()['archive_reader']['messages']==['extra-message0']
            a.tap(40,32);returned=a.diag();assert returned['view']=='archives' and returned['scroll_y']==scrolled,(scrolled,returned['view'],returned['scroll_y'])
        finally:a.close()
    Stream.release.clear()
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),Stream) as server,tempfile.TemporaryDirectory(prefix='penchat-archive-stream-') as directory:
        threading.Thread(target=server.serve_forever,daemon=True).start();folder=pathlib.Path(directory);fixture(folder,server.server_port);a=t.Native(BINARY,folder)
        try:
            a.edit('继续当前聊天');a.tap(908,232);t.wait_for(lambda:a.diag()['generating']);active=a.diag()['session']
            open_archives(a);view(a,'archived');Stream.release.set();t.wait_for(lambda:not a.diag()['generating'])
            assert a.diag()['view']=='archive_chat' and a.diag()['session']==active
            assert status(folder,'archived')=='archived'
            with sqlite3.connect(folder/'penchat.db') as db:
                assert db.execute("select output_text,status from chat_messages where session_id=? order by seq desc limit 1",(active,)).fetchone()==('stream','complete')
                assert db.execute("select count(*) from chat_messages where session_id='archived'").fetchone()[0]==3
            a.to_chat();assert a.diag()['session']==active
        finally:Stream.release.set();a.close();server.shutdown()
    print('Archive reader: read-only title/view entry, images, Markdown/math, reasoning, seq pagination, list return, draft/model and concurrent generation preservation passed.')

if __name__=='__main__':run()
