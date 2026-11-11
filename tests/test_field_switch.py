#!/usr/bin/env python3
"""Portrait focus transfers retain keyboard/OCR ownership and isolate field state."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
from provider_ui import activate
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/field-switch';OUT.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='penchat-field-switch-') as folder:
    a=t.Native(BINARY,folder,official_adapter=False,screen={'width':568,'height':1210})
    try:
        def e():return a.diag()['editor']
        def settle():t.wait_for(lambda:e()['active'] and not e()['ocr_preparing'] and bool(e()['keys']))
        def key(name):
            k=next(k for k in e()['keys'] if k['key']==name);a.tap(k['x'],k['y'])
        def focus(name):activate(a,'field:'+name);settle()
        def text():
            a.editor_action('official');r=a.receive('keyboard.open');a.send('keyboard.result',requestId=r['requestId'],confirmed=False);settle();return r['text']
        a.open_settings();a.setting('models');a.page_action('添加 Provider');focus('name')
        old=e();keyboard=old['keyboard_session'];generation=a.diag()['scanner'].get('generation')
        for c in 'nihao':key(c)
        old=e();focus('baseURL')
        url=e();assert url['keyboard_session']==keyboard and url['session']!=old['session']
        assert a.diag()['scanner'].get('generation')==generation
        assert not any(v.get('type')=='ocr.cancel' for v in a.pending)
        assert not any(v.get('type')=='editor.state' and not v.get('active') for v in a.pending)
        # Old field asynchronous results cannot append to the newly focused URL.
        n=url['length'];a.send('ocr.result',requestId=old['ocr_request'],session=old['session'],mode='single',capture=1,confirmed=True,text='stale')
        assert e()['length']==n
        for c in 'abc':key(c)
        # Models now have their own page. Keep this transfer check on actual
        # Provider fields, using stable locators rather than removed model coordinates.
        url=e();key('x');same=e()
        assert same['keyboard_session']==keyboard and same['build_count']==url['build_count']
        capture(a,OUT/'url-focus.png')
        focus('apiKey');assert e()['secret'] and e()['keyboard_session']==keyboard and not e()['preedit_length']
        key('a');a.editor_action('cancel');t.wait_for(lambda:not e()['active'])
        focus('baseURL');assert text()=='https://api.openai.com/v1abcx'
        a.editor_action('cancel');focus('name');assert text()=='OpenAInihao'
        a.editor_action('cancel');focus('apiKey');assert e()['length']==0
        # A non-input navigation action closes the retained keyboard normally.
        activate(a,'provider_advanced');t.wait_for(lambda:not e()['active'])
        assert not e()['keyboard_session'] and not e()['ocr_active'] and not e()['actions']
        print('Field switching: stable keyboard ownership, raw preedit commit, unchanged compatible key caps, stale OCR, secret/cancel and navigation cleanup passed.',flush=True)
    finally:a.close()
