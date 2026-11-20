#!/usr/bin/env python3
"""Global text scaling keeps history titles, dates and archive actions separated."""
import json
import os
import pathlib
import sqlite3
import tempfile
import test_lvgl_app as t
from image_gestures import capture

t.port = 1
BINARY = pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY', t.ROOT/'build/host/penchat-lvgl'))
OUT = t.ROOT/'.work/release-0.3.1/history-font-size'
OUT.mkdir(parents=True, exist_ok=True)

def check(app, archived=False):
    d = app.diag()
    captions = sorted(d['history_captions'].items(), key=lambda item: item[1]['row_bounds'][1])
    assert len(captions) == (3 if archived else 6)
    previous = None
    for identity, item in captions:
        row = item['row_bounds']
        for kind in ('title', 'time'):
            b = item[kind+'_bounds']
            assert b and b[3]-b[1]+1 >= item[kind+'_line_height'], item
            assert row[1] <= b[1] <= b[3] <= row[3], item
        assert item['title_bounds'][2] < item['time_bounds'][0], item
        if previous is not None:
            assert row[1] > previous, captions
        previous = row[3]
        if archived:
            action = d['archive_views'][identity]
            if d['width'] <= 640 or d['height'] > d['width']:
                assert action[1] > row[3], (item, action)
                previous = action[3]
    return d

for width, height in ((960, 266), (640, 260), (568, 1210)):
    for percent in (75, 100, 150):
        with tempfile.TemporaryDirectory(prefix='penchat-history-font-') as folder:
            path = pathlib.Path(folder)
            schema = (t.ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
            with sqlite3.connect(path/'penchat.db') as db:
                db.executescript(schema)
                for key, value in (('reading_font_percent', percent), ('history_time_display', 'time')):
                    db.execute('insert into app_settings values(?,?,1)', (key, json.dumps(value)))
                for n in range(9):
                    db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values(?,?,?,?,?,?)",
                               (str(n), '历史会话字号验证 '+str(n), 'active' if n < 6 else 'archived', 1, n+1, 1780000000000))
                    db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text) values(?,?,1,'assistant','complete','text',1,1,'字号测试内容')",
                               ('message-'+str(n), str(n)))
            app = t.Native(BINARY, path, seed_model=False, official_adapter=False, screen={'width': width, 'height': height})
            try:
                app.open_navigation()
                t.wait_for(lambda: not app.diag().get('surface_motion', False))
                check(app)
                capture(app, OUT/f'{width}-{percent}-history.png')
                app.open_settings();app.setting('archives')
                check(app, True)
                capture(app, OUT/f'{width}-{percent}-archives.png')
                print(f'{width}x{height} text {percent}%: history title/date and archive action geometry passed.', flush=True)
            finally:
                app.close()
