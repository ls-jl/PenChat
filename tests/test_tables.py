#!/usr/bin/env python3
import os,pathlib,tempfile,time
from test_lvgl_app import Native,ROOT
import test_lvgl_app as transport
from test_chat_style import seed,snapshot
transport.port=1
text='| 长列标题 | 居中 | 右对齐 |\n| :--- | :---: | ---: |\n| '+'宽列测试'*25+' | 12 | $\\frac{1}{2}$ |\n| short | 3 | 456 |\n\n| 独立表格 |\n| --- |\n| 不随上表移动 |\n'
with tempfile.TemporaryDirectory(prefix='penchat-tables-') as directory:
    seed(directory,text)
    app=Native(pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl')),pathlib.Path(directory))
    try:
        rows=app.diag()['table_rows'];assert len(rows)==5
        first=rows[0]['id'];second=rows[-1]['id'];assert first!=second
        assert len(set(r['width'] for r in rows if r['id']==first))==1
        y=next(r['y']+15 for r in rows if r['id']==first and 5<=r['y']<=170)
        for x in range(720,399,-20):
            app.send('touch',x=x,y=y,pressed=True);time.sleep(.02)
        app.send('touch',x=400,y=y,pressed=False);time.sleep(1.3)
        rows=app.diag()['table_rows']
        offsets=[r['scroll'] for r in rows if r['id']==first]
        assert min(offsets)>0 and len(set(offsets))==1,rows
        assert all(r['scroll']==0 for r in rows if r['id']==second)
        snapshot(app,'table-synchronized-scroll')
        app.open_settings();app.to_chat()
        rebuilt=app.diag()['table_rows']
        assert [r['scroll'] for r in rebuilt if r['id']==first]==offsets,(offsets,rebuilt)
        assert all(r['scroll']==0 for r in rebuilt if r['id']==second)
    finally:app.close()
print('Tables: shared widths, independent table identities and synchronized horizontal scrolling passed.')
