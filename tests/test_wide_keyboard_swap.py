#!/usr/bin/env python3
"""Actual LVGL split-editor region swap and explicit globe-menu official entry."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture

t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/wide-keyboard-swap';OUT.mkdir(parents=True,exist_ok=True)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-wide-swap-') as folder:
        def launch():
            return t.Native(BINARY,folder,seed_model=False,official_adapter=False,
                            screen={'width':w,'height':h,'direction':0})
        a=launch()
        try:
            def e():return a.diag()['editor']
            def point(x,y):
                s=e()['coordinate_space']
                return round(x*w/s['width']),round(y*h/s['height'])
            def key(name):
                k=next(k for k in e()['keys'] if k['key']==name)
                a.tap(*point(k['x'],k['y']))
            def open_editor():
                b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
                t.wait_for(lambda:e()['active'] and bool(e()['keys']))
                if e().get('compact_full'):a.editor_action('compact-view')
            def menu():
                k=next(k for k in e()['keys'] if k['key']=='language');x,y=point(k['x'],k['y'])
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.57)
                assert e()['layout_menu'],'500ms globe hold must open before release'
                a.send('touch',x=x,y=y,pressed=False);time.sleep(.06)
                assert e()['layout_menu'] and 'official' in e()['actions']
                b=e()['layout_popup'];space=e()['coordinate_space']
                assert b[0]>=0 and b[1]>=0 and b[2]<space['width'] and b[3]<space['height']
            def change_setting():
                a.open_settings();a.setting('keyboard')
                b=a.diag()['settings']['rows']['wide_keyboard_swap']['bounds']
                a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);a.to_chat()
            def close_editor():a.editor_action('cancel' if 'cancel' in e()['actions'] else 'done')
            wide=w>=h
            open_editor();assert not e()['keyboard_swapped'] and 'official' not in e()['actions']
            capture(a,OUT/f'{w}-normal.png')
            # Short taps retain only the three internal layouts, never the IME.
            for expected in ['en26','zh9','zh26']:
                key('language');assert e()['layout']==expected and not a.diag()['keyboard']
                assert 'official' not in e()['actions']
            close_editor();change_setting();open_editor()
            assert e()['keyboard_swapped']==wide
            capture(a,OUT/f'{w}-swapped.png')
            bounds=e()['text_bounds'];keys={k['key']:k for k in e()['keys']}
            if wide:
                assert bounds[0]==8 and bounds[2]<e()['keyboard_origin']
                assert keys['q']['x']>bounds[2] and keys['q']['x']<keys['p']['x']
                assert all(v[0]<e()['keyboard_origin'] for v in e()['actions'].values())
            else:
                assert e()['keyboard_origin']==0
            menu();capture(a,OUT/f'{w}-globe-menu.png');a.editor_action('layout:1')
            assert not e()['layout_menu'] and e()['layout']=='en26'
            key('a');key('l');expected='al'
            keys={k['key']:point(k['x'],k['y']) for k in e()['keys']}
            def frame(items):
                points=[{'id':ident,'x':xy[0],'y':xy[1]} for ident,xy in items]
                x,y=items[0][1] if items else (0,0)
                a.send('touch',x=x,y=y,pressed=bool(items),points=points)
            for n in range(12):
                frame([(f'a{n}',keys['a'])]);frame([(f'l{n}',keys['l']),(f'a{n}',keys['a'])])
                frame([(f'l{n}',keys['l'])]);frame([]);expected+='al'
            t.wait_for(lambda:e()['length']==len(expected))
            a.editor_action('clipboard');assert e()['clipboard_open']
            if wide:assert e()['actions']['clip-close'][2]<e()['keyboard_origin']
            capture(a,OUT/f'{w}-clipboard.png');a.editor_action('clip-close')
            a.editor_action('official');request=a.receive('keyboard.open')
            assert request['text']==expected and not e()['layout_menu']
            if wide:assert request['field']['x']<e()['keyboard_origin']
            a.send('keyboard.result',requestId='stale',confirmed=True,text='discard')
            assert a.diag()['keyboard']
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            t.wait_for(lambda:not a.diag()['keyboard'])
            assert e()['length']==len(expected) and not e()['layout_menu']
            # Candidates, expansion and the editing controls follow the editor.
            menu();a.editor_action('layout:0')
            for c in 'nihao':key(c)
            t.wait_for(lambda:e()['candidate_count']>0 and not e()['pending'])
            capture(a,OUT/f'{w}-candidates.png')
            if wide:
                a.editor_action('expand')
                assert e()['expanded'];capture(a,OUT/f'{w}-expanded.png')
                a.editor_action('compact-view' if e().get('compact_full') else 'expand')
                assert not e()['expanded']
            key('enter');t.wait_for(lambda:not e()['preedit_length'])
            if e().get('compact_full'):a.editor_action('compact-view')
            a.editor_action('official');request=a.receive('keyboard.open')
            assert request['text']==expected+'nihao'
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            t.wait_for(lambda:not a.diag()['keyboard'])
            close_editor();a.close();a=launch();open_editor()
            assert e()['keyboard_swapped']==wide,'setting must survive process restart'
            close_editor();change_setting();open_editor()
            assert not e()['keyboard_swapped'],'turning off must restore original geometry'
            close_editor();assert not a.read_messages()
            print(f'{w}x{h}: persistent swap={wide}, typing/rollover, candidate/clipboard regions, 500ms menu and explicit official handoff passed.',flush=True)
        finally:a.close()

def details():
    with tempfile.TemporaryDirectory(prefix='penchat-wide-details-') as folder:
        a=t.Native(BINARY,folder,seed_model=False,official_adapter=False)
        try:
            def e():return a.diag()['editor']
            def key(name):
                k=next(k for k in e()['keys'] if k['key']==name);a.tap(k['x'],k['y'])
            def open_editor():
                b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
                t.wait_for(lambda:e()['active'] and bool(e()['keys']))
            def layout(name):
                k=next(k for k in e()['keys'] if k['key']=='language');a.long_press(k['x'],k['y'],.58)
                a.editor_action('layout:'+str(name))
            a.open_settings();a.setting('keyboard')
            b=a.diag()['settings']['rows']['wide_keyboard_swap']['bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            a.to_chat();open_editor();layout(1)
            key('mode');assert e()['mode']==1;key('1');capture(a,OUT/'960-numbers.png')
            key('symbols');assert e()['mode']==2;key('[');capture(a,OUT/'960-symbols.png')
            key('mode');assert e()['mode']==0
            layout(2)
            for value in '64426':key('t9:'+value)
            t.wait_for(lambda:e()['exact_count'] and not e()['pending'])
            assert next(k for k in e()['keys'] if k['key']=='t9:2')['x']>e()['keyboard_origin']
            capture(a,OUT/'960-nine-filter.png');key('enter')
            a.editor_action('official');request=a.receive('keyboard.open');assert request['text']=='1[64426'
            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='alpha beta 中文 👩‍💻')
            t.wait_for(lambda:not e()['active']);open_editor();layout(1)
            b=e()['text_bounds'];x,y=b[0]+32,b[1]+18
            a.send('touch',x=x,y=y,pressed=True);time.sleep(.52)
            a.send('touch',x=b[2]+80,y=y,pressed=True)
            t.wait_for(lambda:e()['magnifier'] and e()['cursor_drag'])
            bubble=e()['magnifier_bounds']
            assert bubble[0]>=4 and bubble[2]<960-4
            capture(a,OUT/'960-magnifier.png')
            a.send('touch',x=b[2]+80,y=y,pressed=False);time.sleep(.1)
            a.tap(x,y);a.tap(x,y);t.wait_for(lambda:e()['selected'])
            a.long_press(x,y,.55);t.wait_for(lambda:e()['menu'])
            popup=e()['selection_popup'];assert popup[0]>=0 and popup[2]<960
            capture(a,OUT/'960-selection.png');a.editor_action('copy')
            assert e()['clipboard_count']==1
            handle=next(h for h in e()['selection_handles'] if h['endpoint']==0)
            before=(e()['selection_start'],e()['selection_end'])
            a.send('touch',x=handle['x'],y=handle['y']+8,pressed=True)
            a.send('touch',x=b[2]+80,y=handle['y']+8,pressed=True)
            t.wait_for(lambda:(e()['selection_start'],e()['selection_end'])!=before)
            assert e()['active'] and e()['selected'] and e()['magnifier']
            a.send('touch',x=b[2]+80,y=handle['y']+8,pressed=False);time.sleep(.1)
            a.editor_action('cancel');a.open_settings();a.setting('models');a.page_action('添加 Provider');a.tap(250,120)
            t.wait_for(lambda:e()['active'] and e()['secret'])
            assert e()['keyboard_swapped'] and e()['text_bounds'][0]==8
            # The password eye moves with the editor; no old right-side target.
            revealX=e()['text_bounds'][2]+1-102;reveal=[revealX,6,revealX+43,49]
            assert any(c['bounds']==reveal for c in e()['controls'])
            a.tap(revealX+22,28);key('a');capture(a,OUT/'960-secret.png')
            a.editor_action('official');request=a.receive('keyboard.open')
            assert request['text']=='a' and request['inputType']=='EnUSOnly'
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            t.wait_for(lambda:not a.diag()['keyboard'])
            print('Swapped editor detail: numbers/symbols/nine-filter, raw codes, caret/magnifier, selection menu/clipboard and password reveal/official field passed.',flush=True)
        finally:a.close()

if __name__=='__main__':
    for size in [(960,266),(936,280),(568,1210),(640,260)]:run(*size)
    details()
