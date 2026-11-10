#!/usr/bin/env python3
"""Real LVGL reader gestures, transparent shortcut and persisted settings."""
import json, os, pathlib, sqlite3, tempfile, time
import test_lvgl_app as t
from test_chat_style import seed
from image_gestures import capture
from test_image_viewer import png

t.port = 1
BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))
OUT = t.ROOT/'.work/composer-shortcut'
OUT.mkdir(parents=True, exist_ok=True)

def swipe(a, delta=90):
    d = a.diag()
    # Stay above even a two-line composer and settle before releasing.
    start = 65 if delta > 0 else 165
    for i in range(9):
        a.send('touch', x=d['width']//2, y=start+delta*i//8, pressed=True)
        time.sleep(.025)
    time.sleep(.12)
    a.send('touch', x=d['width']//2, y=start+delta, pressed=False)
    time.sleep(.2)

def shortcut(a, double=True):
    d = a.diag(); b = d['composer_shortcut']['bounds']
    point = ((b[0]+b[2])//2, (b[1]+b[3])//2)
    a.tap(*point)
    if double: a.tap(*point)
    time.sleep(.2 if double else .4)
    return point

def select_mode(a, mode):
    a.open_settings()
    a.setting('composer_shortcut')
    assert a.diag()['modal']
    capture(a, OUT/f"{a.diag()['width']}-settings-{mode}.png")
    a.tap(a.diag()['width']//2, 66+52*['auto','manual','off'].index(mode))
    t.wait_for(lambda:not a.diag()['modal'])
    with sqlite3.connect(a.directory/'penchat.db') as db:
        assert json.loads(db.execute("select value_json from app_settings where key='landscape_composer_shortcut'").fetchone()[0]) == mode
    a.to_chat()

for width, height in [(960,266), (936,280), (640,260)]:
    with tempfile.TemporaryDirectory(prefix='penchat-shortcut-') as folder:
        seed(folder, '\n\n'.join(f'阅读与滚动测试，第{i}行。' for i in range(150)))
        a = t.Native(BINARY, folder, official_adapter=False, screen={'width':width,'height':height})
        try:
            d = a.diag()
            assert d['at_latest_bottom'] and not d['composer_hidden']
            assert d['composer_shortcut']['mode'] == 'auto'
            b = d['composer_shortcut']['bounds']
            assert b[2]-b[0]+1 >= 44 and b[3]-b[1]+1 >= 44
            assert b[2] < d['composer_bounds'][0]
            # Visibility changes never append messages.
            with sqlite3.connect(a.directory/'penchat.db') as db:
                before = db.execute('select count(*) from chat_messages').fetchone()[0]
            swipe(a); swipe(a)
            assert not a.diag()['at_latest_bottom'] and a.diag()['composer_hidden']
            swipe(a, -65)
            assert a.diag()['composer_hidden']  # Neither direction reveals it.
            old = a.diag()['scroll_y']
            point = shortcut(a)
            d = a.diag()
            assert d['composer_shortcut']['shown'] and not d['composer_hidden'], d['composer_shortcut']
            assert not d['editor']['active'] and d['scroll_y'] == old and not d['following_latest']
            assert d['composer_shortcut']['limit'] == height*3//2
            capture(a, OUT/f'{width}-revealed.png')
            # Single-tap dismissal and maximum-displacement cancellation.
            shortcut(a, False)
            assert a.diag()['composer_hidden']
            point = shortcut(a)
            for x,y in [point,(point[0]+25,point[1]),point]:
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.03)
            a.send('touch',x=point[0],y=point[1],pressed=False);time.sleep(.45)
            assert a.diag()['composer_shortcut']['shown']
            a.send('touch',x=point[0],y=point[1],pressed=True)
            a.send('touch',x=point[0],y=point[1],pressed=False,cancelled=True)
            time.sleep(.45);assert a.diag()['composer_shortcut']['shown']
            # Actual reader motion, including reversals, expires the override.
            for i in range(12):
                if not a.diag()['composer_shortcut']['shown']: break
                swipe(a, 70 if i%2 == 0 else -65)
            d=a.diag()
            assert not d['at_latest_bottom'] and not d['composer_shortcut']['shown'], {k:d[k] for k in ('scroll_y','at_latest_bottom','composer_shortcut','reader_interacting')}
            assert d['composer_shortcut']['distance'] > height*3//2
            select_mode(a, 'manual')
            swipe(a);shortcut(a)
            for _ in range(7):swipe(a,70)
            assert a.diag()['composer_shortcut']['shown']
            # Opening/closing a keyboard pauses, rather than revoking, reveal.
            d=a.diag();text=d['composer_text_bounds']
            a.tap((text[0]+text[2])//2,(text[1]+text[3])//2)
            t.wait_for(lambda:a.diag()['editor']['active'])
            a.editor_action('official');request=a.receive('keyboard.open')
            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='保留文字草稿')
            t.wait_for(lambda:not a.diag()['editor']['active'])
            assert a.diag()['draft_length'] == len('保留文字草稿')
            assert a.diag()['composer_shortcut']['shown']
            shortcut(a,False);assert a.diag()['composer_hidden']
            select_mode(a,'off');swipe(a);shortcut(a)
            assert a.diag()['composer_hidden'] and not a.diag()['composer_shortcut']['available']
            select_mode(a,'auto');swipe(a);shortcut(a)
            a.send('visibility',visible=False)
            assert not a.diag()['composer_shortcut']['shown']
            # Returning to latest bottom always wins and clears temporary state.
            a.send('visibility',visible=True)
            d=a.diag();a.tap(d['to_bottom_x']+22,d['to_bottom_y']+22)
            t.wait_for(lambda:a.diag()['at_latest_bottom'])
            assert not a.diag()['composer_hidden'] and not a.diag()['composer_shortcut']['shown']
            shortcut(a,False);assert not a.diag()['composer_hidden']
            with sqlite3.connect(a.directory/'penchat.db') as db:
                assert db.execute('select count(*) from chat_messages').fetchone()[0] == before
        finally:a.close()
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height})
        try:
            assert a.diag()['composer_shortcut']['mode']=='auto'
            assert not a.diag()['composer_shortcut']['shown']
        finally:a.close()
        print(f'{width}x{height}: bottom-only, shortcut, gesture expiry, settings and draft retention passed.',flush=True)

with tempfile.TemporaryDirectory(prefix='penchat-shortcut-portrait-') as folder:
    seed(folder,'\n\n'.join('竖屏行为不变。' for _ in range(150)))
    a=t.Native(BINARY,folder,screen={'width':568,'height':1210})
    try:
        swipe(a)
        assert not a.diag()['composer_hidden'] and not a.diag()['composer_shortcut']['available']
        capture(a,OUT/'portrait-unchanged.png')
    finally:a.close()

# Pending images remain attached while a recalled composer hides and reappears.
with tempfile.TemporaryDirectory(prefix='penchat-shortcut-images-') as folder:
    folder=pathlib.Path(folder)
    seed(folder,'\n\n'.join('保留历史和待发送图片。' for _ in range(100)))
    (folder/'assets').mkdir()
    image=folder/'assets/pending.png';png(image,80,60)
    with sqlite3.connect(folder/'penchat.db') as db:
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('photo','fixture',2,'user','complete','image','',1,1)")
        db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,width,height,created_at) values('photo','fixture','photo','image','album',?,?,80,60,1)",(str(image),str(image)))
        db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('photo','fixture','photo',0,'image','photo')")
    a=t.Native(BINARY,folder,official_adapter=False)
    try:
        before=a.read_messages()
        swipe(a);shortcut(a);assert not a.diag()['composer_hidden']
        shortcut(a,False);assert a.diag()['composer_hidden']
        shortcut(a);assert not a.diag()['composer_hidden']
        with sqlite3.connect(folder/'penchat.db') as db:
            assert db.execute("select message_id,stored_path from message_assets where id='photo'").fetchone()==('photo',str(image))
        assert image.exists() and a.read_messages()==before
    finally:a.close()
print('Portrait visibility and pending-image ownership remain unchanged.',flush=True)
