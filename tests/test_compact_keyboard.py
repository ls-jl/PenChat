#!/usr/bin/env python3
"""Short-landscape editor: real keys, dictionary, view changes and field contracts."""
import pathlib, tempfile, time
import test_lvgl_app as t
from image_gestures import capture

t.port = 1
OUT = t.ROOT / '.work/compact-keyboard'
OUT.mkdir(parents=True, exist_ok=True)
for width, height in [(640, 260), (720, 280)]:
    with tempfile.TemporaryDirectory(prefix='penchat-compact-keyboard-') as directory:
        a = t.Native(t.ROOT / 'build/host/penchat-lvgl', pathlib.Path(directory),
                     seed_model=False, official_adapter=False,
                     screen={'width': width, 'height': height, 'direction': 0})
        try:
            def editor():
                return a.diag()['editor']
            def point(x, y):
                s = editor()['coordinate_space']
                return round(x * width / s['width']), round(y * height / s['height'])
            def key(value):
                k = next(k for k in editor()['keys'] if k['key'] == value)
                a.tap(*point(k['x'], k['y']))
            def action(value):
                a.editor_action(value)
            def layout(index):
                k = next(k for k in editor()['keys'] if k['key'] == 'language')
                a.long_press(*point(k['x'], k['y']), .56)
                t.wait_for(lambda: editor()['layout_menu'])
                action('layout:' + str(index))
            def official(text=None):
                action('tools')
                action('official')
                request = a.receive('keyboard.open')
                a.send('keyboard.result', requestId=request['requestId'],
                       confirmed=text is not None, text=text or '')
                t.wait_for(lambda: not a.diag()['keyboard'])
                return request
            def open_editor():
                s = a.diag()
                if s['composer_hidden']:
                    a.send('touch', x=width//2, y=height*2//3, pressed=True)
                    for i in range(1, 9):
                        a.send('touch', x=width//2, y=height*2//3-i*height//16, pressed=True)
                        time.sleep(.02)
                    a.send('touch', x=width//2, y=height//6, pressed=False)
                    t.wait_for(lambda: not a.diag()['composer_hidden'])
                    s = a.diag()
                x1,y1,x2,y2 = s['composer_text_bounds']
                a.tap((x1+x2)//2, max(0,min(height-1,(y1+y2)//2)))
                t.wait_for(lambda: editor()['active'] and bool(editor()['keys']))
            def shot(name):
                capture(a, OUT / f'{width}-{name}.png')

            open_editor()
            e = editor()
            assert e['compact_full'] and not e['coordinate_space']['scaled']
            assert e['text_bounds'][1:] == [0, width - 61, 43]
            session, ocr = e['session'], e['ocr_request']
            assert {'cancel', 'tools', 'done', 'compact-view'} <= set(e['actions'])
            assert not {'ocr-mode', 'clipboard', 'official'} & set(e['actions'])
            for k in e['keys']:
                x1,y1,x2,y2=k['bounds'];assert x2-x1>=44 and y2-y1>=44,k['key']
            shot('default')
            layout(1)
            builds = editor()['build_count']
            for c in 'hello': key(c)
            assert editor()['length'] == 5 and editor()['build_count'] == builds
            # Full editor is the same edit/OCR session, not a field completion.
            action('compact-view')
            e = editor()
            assert e['compact_expanded'] and e['coordinate_space']['scaled']
            assert e['session'] == session and e['ocr_request'] == ocr
            shot('expanded-editor')
            action('compact-view')
            assert editor()['compact_full'] and editor()['length'] == 5
            assert editor()['session'] == session and editor()['ocr_request'] == ocr
            key('x');a.long_press(112,20,.53);t.wait_for(lambda:editor()['menu']);action('undo')
            assert editor()['length']==5,'Undo must survive view changes'
            a.tap(112, 20); a.tap(112, 20)
            t.wait_for(lambda: editor()['selected'] and editor()['menu'])
            shot('selection-menu')
            action('copy')
            action('tools'); shot('tools'); action('clipboard')
            assert editor()['clipboard_open'] and editor()['clipboard_count'] == 1
            shot('clipboard')
            action('clip-paste:0')
            assert editor()['length'] == 5 and not editor()['clipboard_open']
            # The single-line viewport must not flatten newlines or complete/send.
            original = '\nFirst paragraph\n\n第二段 English words 👩‍💻\nLast line'
            official(original)
            assert not editor()['active'] and a.diag()['draft_length'] == len(original)
            open_editor(); assert editor()['compact_full']
            assert 0 <= editor()['caret'][1] - 8 <= 44, editor()['caret']
            shot('long-text')
            action('compact-view'); action('compact-view')
            assert official()['text'] == original
            assert editor()['active'] and editor()['length'] == len(original)
            action('done'); open_editor()
            layout(0)
            before = editor()['length']
            builds = editor()['build_count']
            for c in 'nihao': key(c)
            t.wait_for(lambda: editor()['candidate_count'] > 0 and not editor()['pending'])
            assert editor()['compact_composition'] and editor()['build_count'] == builds
            assert not editor()['text_visible']
            shot('composition')
            action('compact-view'); assert editor()['expanded'] and editor()['compact_full']
            shot('candidates')
            choice = next(name for name in editor()['actions'] if name.startswith('choose:'))
            action(choice)
            t.wait_for(lambda: not editor()['expanded'] and editor()['preedit_length'] == 0)
            assert editor()['compact_full'] and editor()['length'] > before
            # The locked dictionary's seventh nihao candidate is the single
            # character 你. Partial selection closes the grid without losing hao.
            for c in 'nihao': key(c)
            t.wait_for(lambda: editor()['candidate_count'] > 6 and not editor()['pending'])
            action('compact-view')
            choice = next(name for name in editor()['actions'] if name.startswith('choose:') and name.endswith(':6'))
            action(choice)
            t.wait_for(lambda: not editor()['expanded'] and 0 < editor()['preedit_length'] < 5)
            assert editor()['compact_full'] and editor()['compact_composition']
            key('enter');t.wait_for(lambda: not editor()['preedit_length'])
            # Numeric/symbol pages and nine-key pinyin share the full key region.
            key('mode'); assert editor()['mode'] == 1; shot('numbers')
            key('symbols'); assert editor()['mode'] == 2; shot('symbols')
            key('mode'); assert editor()['mode'] == 0
            layout(2); shot('nine-key')
            key('t9:6'); key('t9:4')
            t.wait_for(lambda: editor()['candidate_count'] > 0 and not editor()['pending'])
            assert editor()['exact_count'] > 0
            key('enter'); assert editor()['preedit_length'] == 0
            # Layout changes do not re-register OCR; OCR and mode actions still work.
            current = a.receive('ocr.start')
            while current['requestId'] != editor()['ocr_request']:
                current = a.receive('ocr.start')
            before = editor()['length']
            a.send('ocr.result', requestId=current['requestId'], session=current['session'],
                   mode=current['mode'], capture=1, confirmed=True, text='OCR reviewed')
            t.wait_for(lambda: editor()['length'] == before + len('OCR reviewed'))
            action('tools'); action('ocr-mode')
            t.wait_for(lambda: editor()['ocr_multi'])
            action('cancel')
            assert not editor()['active'] and a.diag()['draft_length'] == len(original)
            open_editor();layout(0)
            for c in 'vvvv':key(c)
            t.wait_for(lambda:not editor()['pending'])
            assert not editor()['candidate_count'] and editor()['compact_composition']
            shot('no-match');action('compact-view')
            assert editor()['compact_full'] and not editor()['expanded'] and not editor()['compact_expanded']
            action('cancel')
            a.open_settings();a.setting('models');a.page_action('添加 Provider');a.tap(200,130)
            t.wait_for(lambda:editor()['active'])
            assert editor()['secret'] and not editor()['chinese'] and not editor()['multiline']
            key('a');shot('secret');action('compact-view')
            assert editor()['compact_expanded'] and editor()['secret']
            action('compact-view');action('tools');action('reveal');shot('secret-revealed')
            action('cancel')
            # Left/right preference applies to the optional split editor too.
            a.to_chat();a.open_settings();a.setting('keyboard')
            b=a.diag()['settings']['rows']['wide_keyboard_swap']['bounds']
            a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);a.to_chat();open_editor()
            assert editor()['compact_full'] and not editor()['keyboard_swapped']
            action('compact-view');e=editor()
            assert e['keyboard_swapped'] and e['text_bounds'][0]==8
            assert e['text_bounds'][2]<e['keyboard_origin']
            assert all(b[2]<e['keyboard_origin'] for b in e['actions'].values())
            layout(1);key('a');key('l');shot('expanded-swapped')
            # A physical left-edge press belongs to the scaled editor, not Back.
            e=editor();b=e['text_bounds'];x,y=point(b[0]+2,b[1]+16)
            a.send('touch',x=x,y=y,pressed=True);time.sleep(.52)
            a.send('touch',x=width-1,y=y,pressed=True)
            t.wait_for(lambda:editor()['cursor_drag'] and editor()['magnifier'])
            assert editor()['active'] and editor()['keyboard_swapped']
            a.send('touch',x=width-1,y=y,pressed=False,cancelled=True)
            action('clipboard')
            assert editor()['actions']['clip-close'][2]<editor()['keyboard_origin']
            action('clip-close')
            action('official');request=a.receive('keyboard.open')
            assert request['field']['x']==8
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            t.wait_for(lambda:not a.diag()['keyboard'])
            assert editor()['keyboard_swapped']
            action('compact-view')
            assert editor()['compact_full'] and not editor()['keyboard_swapped']
            action('compact-view');assert editor()['keyboard_swapped']
            action('cancel');a.close()
            # Persist the same existing setting; each new editor still starts full.
            a=t.Native(t.ROOT/'build/host/penchat-lvgl',pathlib.Path(directory),
                       seed_model=False,official_adapter=False,
                       screen={'width':width,'height':height,'direction':0})
            open_editor();assert editor()['compact_full']
            action('compact-view');assert editor()['keyboard_swapped']
            action('cancel');a.open_settings();a.setting('keyboard')
            b=a.diag()['settings']['rows']['wide_keyboard_swap']['bounds']
            a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);a.to_chat();open_editor()
            action('compact-view');assert not editor()['keyboard_swapped']
            action('cancel')
            assert not a.read_messages(), 'Editor must not send or create chat history'
        finally:
            a.close()
print('COMPACT_FULL_KEYS_CANDIDATES_SAME_SESSION_EXPANSION_SELECTION_CLIPBOARD_OCR_ORIGINAL_TEXT_OK')
