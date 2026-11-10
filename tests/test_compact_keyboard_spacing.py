#!/usr/bin/env python3
"""Compact preedit grows in place, leaving all remaining header width to choices."""
import pathlib,tempfile,time
import test_lvgl_app as t
from image_gestures import capture

t.port=1
OUT=t.ROOT/'.work/compact-keyboard';OUT.mkdir(parents=True,exist_ok=True)
for width,height in [(640,260),(720,280)]:
    with tempfile.TemporaryDirectory(prefix='penchat-preedit-spacing-') as folder:
        a=t.Native(t.ROOT/'build/host/penchat-lvgl',folder,seed_model=False,official_adapter=False,
                   screen={'width':width,'height':height,'direction':0})
        try:
            def e():return a.diag()['editor']
            def key(name):
                k=next(k for k in e()['keys'] if k['key']==name);a.tap(k['x'],k['y'])
            b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            t.wait_for(lambda:e()['active'] and bool(e()['keys']))
            a.receive('ocr.start');time.sleep(.1)
            initial=e();assert initial['actions']['compact-view'][0]-initial['text_bounds'][2]-1==4
            assert initial['actions']['tools'][0]-initial['actions']['cancel'][2]-1==4
            assert initial['text_bounds'][0]-initial['actions']['tools'][2]-1==4
            capture(a,OUT/f'{width}-spacing-empty.png')
            builds=e()['build_count'];lengths=[]
            for letter in 'nihao':
                key(letter);time.sleep(.06)
                state=e();p=state['compact_preedit_bounds'];c=state['compact_candidate_bounds']
                lengths.append(p[2]-p[0]+1)
                assert c[0]-p[2]-1==8
                assert c[2]==state['text_bounds'][2]
                assert state['actions']['compact-view'][0]-c[2]-1==4
                assert c[2]-c[0]+1==state['text_bounds'][2]-state['text_bounds'][0]+1-lengths[-1]-8
                # The first key adopts the newly registered OCR status. Once
                # that lifecycle paint settles, changing preedit width is local.
                if len(lengths)==1:builds=state['build_count']
                else:assert state['build_count']==builds,('Typing rebuilt keys',letter,builds,state['build_count'])
            assert all(x<y for x,y in zip(lengths,lengths[1:])),lengths
            t.wait_for(lambda:e()['candidate_count']>0 and not e()['pending'])
            capture(a,OUT/f'{width}-spacing-nihao.png')
            key('delete');time.sleep(.06)
            assert e()['compact_preedit_bounds'][2]-e()['compact_preedit_bounds'][0]+1<lengths[-1]
            for letter in 'zhongguorenminzhongguorenmin':key(letter)
            state=e();p=state['compact_preedit_bounds']
            assert p[2]-p[0]+1<=144
            assert state['compact_candidate_bounds'][2]-state['compact_candidate_bounds'][0]+1>=state['text_bounds'][2]-state['text_bounds'][0]+1-152
            capture(a,OUT/f'{width}-spacing-long.png')
            a.editor_action('cancel')
            assert not a.read_messages()
        finally:a.close()
print('COMPACT_PREEDIT_GROWS_SHRINKS_CANDIDATES_FILL_REMAINDER_AND_4PX_CONTROL_GAP_OK')
