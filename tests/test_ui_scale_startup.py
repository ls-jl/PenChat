#!/usr/bin/env python3
"""Stored global scaling is normalized before fonts/layout initialize."""
import json,pathlib,sqlite3,tempfile
import test_lvgl_app as t
t.port=1
with tempfile.TemporaryDirectory(prefix='penchat-scale-startup-') as folder:
    p=pathlib.Path(folder)
    schema=(t.ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
    with sqlite3.connect(p/'penchat.db') as db:
        db.executescript(schema)
        db.execute("insert into app_settings values('reading_font_percent','125',1)")
    for raw,expected in [('null',100),('false',100),('"125"',100),('125.5',100),('-100',75),('18446744073709551615',150),('117',117)]:
        with sqlite3.connect(p/'penchat.db') as db:db.execute("insert or replace into app_settings values('ui_scale_percent',?,1)",(raw,))
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',p,seed_model=False)
        try:
            d=a.diag();assert d['ui_scale']['applied']==expected and d['ui_scale']['saved']==expected
            assert d['reading_font']['percent']==125 and d['reading_font']['size']==(21*expected*125+5000)//10000
        finally:a.close()
print('STORED_SCALE_NORMALIZATION_AND_READING_COMPOSITION_OK')
