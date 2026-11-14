#!/usr/bin/env python3
"""Scaled portrait form focus transfer uses actual field and keyboard bounds."""
import pathlib,sqlite3,tempfile
import test_lvgl_app as t
from test_reasoning_ui import seed
from provider_ui import activate

t.port=1
for scale in (75,125,150):
    with tempfile.TemporaryDirectory(prefix='penchat-scaled-edit-') as folder:
        seed(folder,1)
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,official_adapter=False,
                   screen={'width':568,'height':1210},ui_scale_percent=scale)
        try:
            def e():return a.diag()['editor']
            def focus(key):
                activate(a,'field:'+key)
                t.wait_for(lambda:e()['active'] and bool(e()['keys']) and not e()['ocr_preparing'])
            d=a.diag();nav=d['screen_safety']['navigation_x'];top=d['safe_insets'][1]
            a.tap(nav+round(24*scale/100),top+round(24*scale/100))
            t.wait_for(lambda:a.diag()['navigation_open'])
            b=a.diag()['navigation_footer']['settings']['bounds']
            a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            a.setting('models');activate(a,'provider-open:p')
            focus('name');session=e()['keyboard_session']
            key=next(k for k in e()['keys'] if k['key']=='a');a.tap(key['x'],key['y'])
            focus('baseURL');assert e()['keyboard_session']==session
            d=a.diag();field=d['page']['actions']['field:baseURL'];text=e()['text_bounds']
            assert field[0]<=text[0]<text[2]<=field[2] and field[1]<=text[1]<text[3]<=field[3]
            a.editor_action('cancel');t.wait_for(lambda:not e()['active'])
            focus('name');assert e()['length']==len('Fixturea')
            a.editor_action('cancel');t.wait_for(lambda:not e()['active'])
            with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:
                assert db.execute("select name from providers where id='p'").fetchone()[0]=='Fixture'
            print(f'Portrait form {scale}%: bounded editor, retained keyboard, raw preedit and explicit-save isolation passed.',flush=True)
        finally:a.close()
