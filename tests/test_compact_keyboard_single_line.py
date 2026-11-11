#!/usr/bin/env python3
"""Single-line platform: direct clipboard, globe IME and independent secret eye."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture

t.port=1
OUT=t.ROOT/'.work/compact-keyboard';OUT.mkdir(parents=True,exist_ok=True)
previous=os.environ.get('PENCHAT_TEST_SINGLE_LINE_OCR')
os.environ['PENCHAT_TEST_SINGLE_LINE_OCR']='1'
try:
    for width,height in [(640,260),(720,280)]:
        with tempfile.TemporaryDirectory(prefix='penchat-single-line-') as folder:
            a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False,official_adapter=False,
                       screen={'width':width,'height':height,'direction':0})
            try:
                def e():return a.diag()['editor']
                def key(name):
                    k=next(k for k in e()['keys'] if k['key']==name);a.tap(k['x'],k['y'])
                def open_editor():
                    b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
                    t.wait_for(lambda:e()['active'] and bool(e()['keys']))
                def ocr():
                    request=a.receive('ocr.start')
                    while request['requestId']!=e()['ocr_request']:request=a.receive('ocr.start')
                    assert request['mode']=='single'
                    return request
                open_editor();ocr();state=e();session=state['session']
                assert not state['ocr_multiline_supported'] and not state['ocr_multi']
                assert not {'tools','ocr-mode','official'} & set(state['actions'])
                assert state['actions']['clipboard']==[52,0,95,43]
                capture(a,OUT/f'{width}-single-line-toolbar.png')
                a.editor_action('clipboard')
                assert e()['clipboard_open'] and not e()['tools_open']
                capture(a,OUT/f'{width}-single-line-clipboard.png')
                a.editor_action('clip-close');assert e()['session']==session
                # The generic helper must now reach the explicit globe entry.
                a.editor_action('official');request=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard'] and e()['active'])
                current=ocr();assert e()['session']==session
                for capture_id,text in [(1,'第一行'),(2,'第二行')]:
                    a.send('ocr.result',requestId=current['requestId'],session=current['session'],
                           mode='single',capture=capture_id,confirmed=True,text=text)
                    t.wait_for(lambda:e()['length']==capture_id*3)
                a.editor_action('compact-view')
                assert e()['compact_expanded'] and not {'tools','ocr-mode'} & set(e()['actions'])
                a.editor_action('compact-view');a.editor_action('cancel')
                a.open_settings();a.setting('models');a.page_action('添加 Provider');a.page_action('field:apiKey')
                t.wait_for(lambda:e()['active'] and e()['secret'])
                state=e();assert not {'tools','ocr-mode'} & set(state['actions'])
                assert state['text_bounds'][2]<state['actions']['reveal'][0]
                assert state['actions']['reveal'][2]<state['actions']['compact-view'][0]
                key('a');capture(a,OUT/f'{width}-single-line-secret-hidden.png')
                a.editor_action('reveal');capture(a,OUT/f'{width}-single-line-secret-visible.png')
                assert e()['active'] and e()['length']==1
                a.editor_action('official');request=a.receive('keyboard.open')
                assert request['text']=='a' and request['inputType']=='EnUSOnly'
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard'] and e()['active'])
                ocr();a.editor_action('cancel')
                assert not a.read_messages()
            finally:a.close()
finally:
    if previous is None:os.environ.pop('PENCHAT_TEST_SINGLE_LINE_OCR',None)
    else:os.environ['PENCHAT_TEST_SINGLE_LINE_OCR']=previous
print('SINGLE_LINE_PLATFORM_DIRECT_CLIPBOARD_GLOBE_ONLY_IME_OCR_AND_SECRET_EYE_OK')
