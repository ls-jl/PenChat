#!/usr/bin/env python3
"""Short-landscape form names stay readable without shrinking the UI font."""
import json, pathlib, sqlite3, tempfile, time
import test_lvgl_app as ui
from test_reasoning_ui import seed
from provider_ui import activate, capture
from settings_ui import locate

OUT=ui.ROOT/'.work/compact-page-text';OUT.mkdir(parents=True,exist_ok=True)
ui.port=1
BINARY=ui.ROOT/'build/host/penchat-lvgl'

def rows_fit(app):
    rows=app.diag()['settings']['rows']
    ordered=sorted(rows.values(),key=lambda r:r['bounds'][1])
    for row in ordered:
        b=row['bounds'];t=row['title'];d=row['detail'];i=row['indicator']
        # The longest built-in form name is twelve CJK glyphs at 20px. The
        # old dual columns reserved only ~170px after the indicator and clipped it.
        assert t[2]-t[0]+1>=300,(app.diag()['view'],row)
        assert b[0]<=t[0]<=t[2]<i[0] and b[1]<=t[1]<=t[3]<d[1]<=d[3]<=b[3],row
        assert b[3]-b[1]+1>=44
    for a,b in zip(ordered,ordered[1:]):assert a['bounds'][3]<b['bounds'][1]
    return rows

for width,height in [(640,260),(640,360)]:
    with tempfile.TemporaryDirectory(prefix='penchat-compact-text-') as folder:
        p=pathlib.Path(folder);seed(p,1)
        with sqlite3.connect(p/'penchat.db') as db:
            presets=[{'id':'default','name':'默认预设','prompt_mode':'inherit','model_id':'','effort':''}]
            db.execute("insert into app_settings values('chat_presets',?,1)",(json.dumps(presets),))
            db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('archived','归档会话的标题应保留完整的阅读宽度','archived',1,1,1)")
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('archived-answer','archived',1,'assistant','complete','text','归档示例',1,1)")
        a=ui.Native(BINARY,p,screen={'width':width,'height':height,'direction':0})
        try:
            a.open_settings();rows=rows_fit(a)
            assert rows['presets']['bounds'][1]>rows['models']['bounds'][3]
            capture(a,OUT,f'{width}x{height}-settings')
            a.setting('retry_archive_prompt')
            assert a.diag()['settings']['rows']['retry_archive_prompt']['checked']
            locate(a.diag,a.settings_swipe,'keyboard');before=a.diag()['scroll_y']
            a.setting('keyboard');rows_fit(a);capture(a,OUT,f'{width}x{height}-keyboard-settings')
            a.tap(40,32);assert a.diag()['scroll_y']==before
            a.setting('presets');capture(a,OUT,f'{width}x{height}-presets')
            activate(a,'新增预设');capture(a,OUT,f'{width}x{height}-preset-editor')
            a.to_chat();a.open_settings();a.setting('models')
            capture(a,OUT,f'{width}x{height}-model-list')
            activate(a,'model-edit:a');capture(a,OUT,f'{width}x{height}-model-editor')
            a.to_chat();a.open_settings();a.setting('message_edit')
            capture(a,OUT,f'{width}x{height}-edit-options')
            a.tap(width-70,height-44)
            a.to_chat();a.open_settings();a.setting('archives')
            capture(a,OUT,f'{width}x{height}-archives')
            view=next(iter(a.diag()['archive_views'].values()))
            assert view[0]<width//2 and view[2]-view[0]+1>120,view
            a.to_chat();a.open_settings();a.setting('reasoning')
            capture(a,OUT,f'{width}x{height}-reasoning-options')
            # The first choice lives in the modal reader rather than three
            # undersized columns. It still applies its original persisted value.
            a.tap(300,64);ui.wait_for(lambda:not a.diag()['modal'])
            with sqlite3.connect(p/'penchat.db') as db:
                assert json.loads(db.execute("select value_json from app_settings where key='reasoning_display'").fetchone()[0])=='collapsed'
            a.setting('font_size');capture(a,OUT,f'{width}x{height}-font')
        finally:a.close()
# Both other layout classes retain their established structure.
for width,height in [(960,266),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-text-other-layout-') as folder:
        a=ui.Native(BINARY,folder,screen={'width':width,'height':height,'direction':0})
        try:
            a.open_settings();rows=a.diag()['settings']['rows']
            if width>height:assert rows['models']['bounds'][1]==rows['presets']['bounds'][1]
            else:assert rows['presets']['bounds'][1]>rows['models']['bounds'][3]
        finally:a.close()
print('COMPACT_FORM_WIDTH_BASELINES_ARCHIVE_TITLE_OPTIONS_SCROLL_AND_OTHER_LAYOUTS_OK')
