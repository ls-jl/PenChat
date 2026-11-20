#!/usr/bin/env python3
"""Actual LVGL table layout after a paragraph; code/pipes remain literal."""
import json,os,pathlib,sqlite3,tempfile
from test_ui_theme import App,fixture,t
from image_gestures import capture
OUT=t.ROOT/'.work/markdown-tables-ui';OUT.mkdir(parents=True,exist_ok=True)
SOURCE='**示例仓库**\n| 名称 | 说明 | 语言 |\n|---|:---|---:|\n| demo | 完整说明 | C++ |\n\n```text\n示例代码\n| A | B |\n|---|---|\n| 1 | 2 |\n```\n\n普通竖线 A | B | C，不转换。'
for w,h in [(568,1210),(960,266),(640,260)]:
 for percent in (100,150):
  with tempfile.TemporaryDirectory(prefix='penchat-md-table-') as folder:
   p=pathlib.Path(folder);fixture(p)
   with sqlite3.connect(p/'penchat.db') as db:
    db.execute("update chat_messages set output_text=? where id='a'",(SOURCE,))
    db.execute("insert or replace into app_settings values('reading_font_percent',?,1)",(json.dumps(percent),))
   (p/'feedback.json').write_text('{}');(p/'update.json').write_text(json.dumps({'root':'http://127.0.0.1:1/penchat-rk/','nodes':['127.0.0.1'],'auto':False}))
   os.environ.update(PENCHAT_TEST_Y07_ROUNDED='1' if h>w else '0',PENCHAT_TEST_FEEDBACK_CONFIG=str(p/'feedback.json'),PENCHAT_TEST_UPDATE_CONFIG=str(p/'update.json'))
   a=App(p,w,h)
   try:
    d=a.diag();rows=d['table_rows']
    assert len(rows)==2 and len({row['id'] for row in rows})==1,rows
    a.top();capture(a,OUT/f'{w}-{percent}.png')
    with sqlite3.connect(p/'penchat.db') as db:assert db.execute("select output_text from chat_messages where id='a'").fetchone()[0]==SOURCE
   finally:a.close()
  print(w,h,percent,'table rows, code isolation and unchanged source passed',flush=True)
