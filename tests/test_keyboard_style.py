#!/usr/bin/env python3
"""Shared keyboard surfaces and content-sized composition across all layouts."""
import json,os,pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture

t.port=1
OUT=t.ROOT/'.work/keyboard-style';OUT.mkdir(parents=True,exist_ok=True)
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
for width,height,split in [(640,260,False),(640,260,True),(960,266,False),(568,1210,False)]:
    name=f'{width}-'+('split' if split else 'default')
    with tempfile.TemporaryDirectory(prefix='penchat-keyboard-style-') as directory:
        a=t.Native(BINARY,directory,seed_model=False,official_adapter=False,
                   screen={'width':width,'height':height,'direction':0})
        try:
            def e():return a.diag()['editor']
            def point(x,y):
                s=e()['coordinate_space'];return round(x*width/s['width']),round(y*height/s['height'])
            def key(value):
                k=next(k for k in e()['keys'] if k['key']==value);a.tap(*point(k['x'],k['y']))
            def shot(state):
                time.sleep(.12);capture(a,OUT/f'{name}-{state}.png')
            def spacing_check():
                state=e();space=state['coordinate_space'];sx=width/space['width'];sy=height/space['height']
                rows={}
                for k in state['keys']:rows.setdefault(k['bounds'][1],[]).append(k['bounds'])
                for row in rows.values():
                    row.sort()
                    for left,right in zip(row,row[1:]):
                        assert abs((right[0]-left[2])*sx-4)<=1.25,(name,left,right)
                top=sorted(rows)[:3]
                for y,next_y in zip(top,top[1:]):
                    assert abs((next_y-rows[y][0][3])*sy-6)<=1.25,(name,y,next_y)
            def choose_layout(value):
                k=next(k for k in e()['keys'] if k['key']=='language')
                a.long_press(*point(k['x'],k['y']),.56);t.wait_for(lambda:e()['layout_menu'])
                a.editor_action('layout:'+str(value))
            def wait_choices():t.wait_for(lambda:e()['candidate_count']>0 and not e()['pending'])
            def row_check():
                state=e();p=state['composition_bounds'];c=state['candidate_bounds']
                # Bounds share EditorLayout logical coordinates, including the split canvas.
                assert p[1]==c[1] and p[3]-p[1]+1>=42,(name,p,c)
                assert 2<=c[0]-p[2]-1<=5,(name,p,c)
                assert p[2]-p[0]+1<=145 and c[2]>c[0],(name,p,c)
                if state['preedit_length']<=5:assert state['composition_scroll']==0,(name,state)
                return p[2]-p[0]+1,c[2]-c[0]+1
            def expansion():
                return 'compact-view' if e()['compact_full'] else 'expand'
            b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            t.wait_for(lambda:e()['active'] and bool(e()['keys']))
            if split:a.editor_action('compact-view')
            session=e()['session'];keys_before=e()['keys'];spacing_check();shot('empty')
            widths=[]
            for letter in 'nihao':
                key(letter);time.sleep(.07)
                widths.append(row_check()[0]);state=e()
                if len(widths)==1:builds=state['build_count']
                else:assert state['build_count']==builds,(name,'typing rebuilt key caps')
            assert all(x<y for x,y in zip(widths,widths[1:])),(name,widths)
            wait_choices();shot('composition')
            before=e();button=before['actions'][expansion()]
            a.editor_action(expansion());assert e()['expanded']
            if not split:assert e()['actions'][expansion()]==button,(name,'expand target moved')
            assert e()['session']==session
            if not split and height<=width:assert e()['keys']==keys_before
            if height>width:
                panel=e()['candidate_bounds'];first=next(k for k in keys_before if k['key']=='q')
                assert panel[1]>=first['bounds'][1] and e()['text_in_target']
            shot('expanded')
            a.editor_action(expansion());assert not e()['expanded']
            row_check()
            key('space');t.wait_for(lambda:e()['length']>0 and not e()['preedit_length'])
            assert not e()['composition_visible'] and e()['session']==session
            if split and e()['compact_full']:a.editor_action('compact-view')
            if height>width:assert {'ocr-mode','clipboard'}<=set(e()['actions'])
            for letter in 'zhongguorenminzhongguorenmin':key(letter)
            row_check();shot('long-composition');key('enter')
            assert not e()['preedit_length'] and not e()['composition_visible']
            # Number/symbol pages preserve punctuation semantics and restore the layout.
            key('mode');assert e()['mode']==1;spacing_check();shot('numbers');key(',');key('.')
            key('symbols');assert e()['mode']==2;spacing_check();shot('symbols');key(',');key('.')
            key('mode');assert e()['mode']==0
            choose_layout(1);assert not e()['composition_visible'];shot('english')
            key('shift');assert e()['shift']==1;shot('shift')
            key('A');assert e()['shift']==0
            key('shift');key('shift');assert e()['shift']==2;shot('shift-lock');key('shift')
            key('b')
            if 'tools' in e()['actions']:a.editor_action('tools')
            a.editor_action('clipboard');assert e()['clipboard_open'];shot('clipboard')
            a.editor_action('clip-close');assert e()['session']==session
            a.editor_action('official');request=a.receive('keyboard.open')
            assert '你好' in request['text'] and ',.,.' in request['text'],request['text']
            a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
            t.wait_for(lambda:e()['active'] and not a.diag()['keyboard']);assert e()['session']==session
            choose_layout(2)
            for digit in '64426':key('t9:'+digit)
            wait_choices();row_check();spacing_check();shot('nine-key')
            key('enter');assert not e()['preedit_length']
            if split:
                a.editor_action('compact-view');assert e()['compact_full']
                a.editor_action('compact-view');assert e()['compact_expanded']
            a.editor_action('done');t.wait_for(lambda:not e()['active']);assert not a.read_messages()
            print(name+': shared composition, expansion, incremental typing, modes, clipboard and official recovery passed',flush=True)
        except BaseException:
            (OUT/f'{name}-failure.json').write_text(json.dumps(a.diag(),ensure_ascii=False,indent=2));shot('failure');raise
        finally:a.close()
