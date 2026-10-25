#!/usr/bin/env python3
"""Independent pinyin UI; real dictionaries, no launcher candidate mocks."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from image_gestures import capture
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=pathlib.Path(os.environ.get('PENCHAT_PINYIN_ARTIFACTS',t.ROOT/'.work/pinyin-ui'));OUT.mkdir(parents=True,exist_ok=True)
t.port=1

def editor(a):return a.diag()['editor']
def key(a,name):
    k=next(k for k in editor(a)['keys'] if k['key']==name);a.tap(k['x'],k['y'])
def open_editor(a):
    s=a.diag();a.tap(s['width']//2,s['height']-34);t.wait_for(lambda:editor(a)['active'])
def layout(a,name):
    k=next(k for k in editor(a)['keys'] if k['key']=='language')
    a.send('touch',x=k['x'],y=k['y'],pressed=True);time.sleep(.55)
    a.send('touch',x=k['x'],y=k['y'],pressed=False)
    t.wait_for(lambda:editor(a)['layout_menu'])
    a.editor_action('layout:'+str({'zh26':0,'en26':1,'zh9':2}[name]));t.wait_for(lambda:editor(a)['layout']==name)
def ready(a):return t.wait_for(lambda:editor(a)['candidate_count']>0 and not editor(a)['pending'])
def selected(a):return t.wait_for(lambda:not editor(a)['preedit_length'] and not editor(a)['pending'])
def official(a,confirmed=False,text=''):
    a.editor_action('official');request=a.receive('keyboard.open')
    a.send('keyboard.result',requestId=request['requestId'],confirmed=confirmed,text=text)
    t.wait_for(lambda:not a.diag()['keyboard']);return request

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-pinyin-ui-') as directory:
        folder=pathlib.Path(directory);profile=folder/'ime/user.json'
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            open_editor(a);assert editor(a)['layout']=='zh26'
            for value in ['en26','zh9','zh26']:
                key(a,'language');assert editor(a)['layout']==value
            for c in 'ni':key(a,c)
            ready(a);capture(a,OUT/f'{w}-26-composition.png')
            key(a,'language');assert editor(a)['layout']=='en26' and editor(a)['parked_count']==1
            key(a,'x');key(a,'language');assert editor(a)['layout']=='zh26' and editor(a)['preedit_length']==2
            ready(a);key(a,'space');selected(a)
            text=official(a)['text'];assert text.startswith('x') and len(text)==2
            assert not profile.exists(),'Selection must not persist before completion'
            a.editor_action('done');t.wait_for(lambda:not editor(a)['active']);t.wait_for(profile.exists)
            saved=json.loads(profile.read_text());assert saved['records']
            open_editor(a);layout(a,'zh9');assert editor(a)['preferred_layout']=='zh26'
            for digit in '64426':key(a,'t9:'+digit)
            ready(a);assert editor(a)['preferred_layout']=='zh9' and editor(a)['exact_count']>0
            a.tap(w-30,28);assert editor(a)['expanded']
            key(a,'t9:6');assert editor(a)['preedit_length']==6 and not editor(a)['expanded']
            key(a,'delete');ready(a);assert editor(a)['preedit_length']==5
            capture(a,OUT/f'{w}-nine.png')
            names=[v for v in editor(a)['actions'] if v.startswith('exact:')]
            assert names;a.editor_action(names[0]);ready(a)
            assert editor(a)['preedit_length']==5
            capture(a,OUT/f'{w}-nine-filter.png')
            key(a,'enter');assert editor(a)['preedit_length']==0
            assert official(a)['text'].endswith('64426')
            key(a,'punctuation');assert editor(a)['punctuation_menu'];capture(a,OUT/f'{w}-nine-punctuation.png')
            a.editor_action('punct:，');assert official(a)['text'].endswith('64426，')
            key(a,'language');assert editor(a)['layout']=='en26';key(a,'a');key(a,'language');assert editor(a)['layout']=='zh9'
            time.sleep(2.05);key(a,'language');assert editor(a)['layout']=='en26'
            time.sleep(2.05);key(a,'language');assert editor(a)['layout']=='zh9'
            a.editor_action('cancel');open_editor(a);assert editor(a)['layout']=='zh9'
            # Two parked compositions survive an official cancellation.
            layout(a,'zh26');key(a,'n');key(a,'i');layout(a,'zh9');key(a,'t9:6');key(a,'t9:4');layout(a,'en26')
            assert editor(a)['parked_count']==2
            capture(a,OUT/f'{w}-english-parked.png')
            before=editor(a);official(a)
            assert editor(a)['layout']=='en26' and editor(a)['parked_count']==2 and editor(a)['cursor']==before['cursor']
            # Finishing English discards parked compositions without prompting.
            a.editor_action('done');assert not editor(a)['active'] and not a.diag()['modal']
            open_editor(a);assert editor(a)['layout']=='zh9' and editor(a)['parked_count']==0 and editor(a)['preedit_length']==0
            layout(a,'zh26');capture(a,OUT/f'{w}-26.png')
            for c in 'nihao':key(a,c)
            ready(a);key(a,'space');selected(a)
            old=profile.read_bytes();a.editor_action('cancel');time.sleep(.08);assert profile.read_bytes()==old
            # Explicit layout selection never writes the preferred Chinese layout.
            open_editor(a);layout(a,'zh9');a.editor_action('cancel');open_editor(a);assert editor(a)['layout']=='zh26'
            # A touch cancellation/slide out of the globe must not switch modes.
            k=next(k for k in editor(a)['keys'] if k['key']=='language');before=editor(a)['layout']
            a.send('touch',x=k['x'],y=k['y'],pressed=True);a.send('touch',x=w-3,y=2,pressed=True)
            a.send('touch',x=k['x'],y=k['y'],pressed=False,cancelled=True);time.sleep(.1);assert editor(a)['layout']==before
            a.editor_action('cancel')
            a.open_settings();a.setting('keyboard');assert a.diag()['view']=='keyboard_settings'
            capture(a,OUT/f'{w}-settings.png')
            row=a.diag()['settings']['rows']['pinyin_learning']['bounds'];a.tap((row[0]+row[2])//2,(row[1]+row[3])//2)
            assert not a.diag()['settings']['rows']['pinyin_learning']['checked']
            a.to_chat();open_editor(a);layout(a,'zh26')
            for c in 'nihao':key(a,c)
            ready(a);key(a,'space');selected(a)
            layout(a,'en26');a.editor_action('clipboard');assert editor(a)['clipboard_open']
            length=editor(a)['length'];key(a,'x');assert editor(a)['length']==length+1
            a.tap(w-30,28);assert not editor(a)['clipboard_open']
            a.editor_action('done');time.sleep(.12);assert profile.read_bytes()==old
            a.open_settings();a.setting('keyboard')
            row=a.diag()['settings']['rows']['clear_pinyin']['bounds'];a.tap((row[0]+row[2])//2,(row[1]+row[3])//2)
            assert a.diag()['modal'];capture(a,OUT/f'{w}-clear-confirm.png');a.tap(200,h-44)
            t.wait_for(lambda:json.loads(profile.read_text())['records']==[])
            a.tap(w-70,h-44);a.to_chat()
        finally:a.close()
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            open_editor(a);assert editor(a)['layout']=='zh26' and not editor(a)['parked_count']
            assert json.loads(profile.read_text())['records']==[]
        finally:a.close()
    print(f'Independent pinyin UI {w}x{h}: three layouts, globe cycle/timeout, raw return, parked input, completion/cancel learning, filters/settings and restart passed.',flush=True)

if __name__=='__main__':
    for size in [(960,266),(936,280)]:run(*size)
