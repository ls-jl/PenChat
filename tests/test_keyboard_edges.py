#!/usr/bin/env python3
"""Actual touch regression for staggered-row edge gaps, jitter and cancellation."""
import json,os,pathlib,tempfile,time
import test_lvgl_app as t
import test_pinyin_ui as p
from image_gestures import capture
OUT=t.ROOT/'.work/keyboard-edges-0.2.10';OUT.mkdir(parents=True,exist_ok=True)
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
def keys(a):return {k['key']:k for k in p.editor(a)['keys']}
def move(a,points,cancel=False):
    for x,y in points:a.send('touch',x=x,y=y,pressed=True);time.sleep(.03)
    x,y=points[-1];a.send('touch',x=x,y=y,pressed=False,cancelled=cancel);time.sleep(.12)
def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-keyboard-edges-') as folder:
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            p.open_editor(a);p.layout(a,'en26');k=keys(a);e=p.editor(a)
            split=e.get('keyboard_split',e['text_bounds'][0]-8);y=k['a']['y'];expected=''
            # These outer-row points were dead zones in 0.2.9; exact text matters.
            for name,xs in [('a',[0,3,15,k['a']['x']]),('l',[split-1,split-4,split-15,k['l']['x']])]:
                for x in xs:
                    a.tap(x,y);expected+=name
                    assert p.editor(a)['length']==len(expected),(w,name,x,p.editor(a)['length'],len(expected))
            assert p.official(a)['text']==expected
            # Slight drift is still the original key, not a failed back gesture.
            move(a,[(3,y),(13,y),(23,y)]);expected+='a'
            move(a,[(split-2,y),(split-10,y)]);expected+='l'
            assert p.official(a)['text']==expected
            for name in ['s','k']:k=keys(a);a.tap(k[name]['x'],k[name]['y']);expected+=name
            # Shift and lock use the same geometry, with the correct characters.
            p.key(a,'shift');k=keys(a);a.tap(3,k['A']['y']);expected+='A'
            p.key(a,'shift');p.key(a,'shift');assert p.editor(a)['shift']==2
            k=keys(a);a.tap(split-2,k['L']['y']);expected+='L';p.key(a,'shift')
            assert p.official(a)['text']==expected
            k=keys(a);assert k['a']['hit_bounds'][0]==0 and k['l']['hit_bounds'][2]==split
            assert k['s']['hit_bounds'][0]>k['a']['bounds'][0]
            capture(a,OUT/f'{w}-english.png')
            # Leaving the original hit region cancels permanently, even on return.
            before=p.editor(a)['length'];right=k['a']['hit_bounds'][2]
            move(a,[(3,y),(right+30,y),(3,y)]);assert p.editor(a)['length']==before
            move(a,[(split-2,y),(split-2,y+95),(split-2,y)]);assert p.editor(a)['length']==before
            move(a,[(split-2,y),(split-3,y)],True);assert p.editor(a)['length']==before
            # Multi-touch and foreground interruption cannot commit held keys.
            a.send('touch',x=split-2,y=y,pressed=True)
            a.send('touch',x=split-2,y=y,pressed=True,points=[{'id':1,'x':split-2,'y':y},{'id':2,'x':200,'y':y}])
            a.send('touch',x=split-2,y=y,pressed=False,points=[]);time.sleep(.1)
            assert p.editor(a)['length']==before
            a.send('touch',x=3,y=y,pressed=True);a.send('visibility',visible=False);a.send('visibility',visible=True)
            a.send('touch',x=3,y=y,pressed=False,cancelled=True);time.sleep(.1);assert p.editor(a)['length']==before
            a.tap(split-2,y);expected+='l';assert p.official(a)['text']==expected
            # The right-side gutter/editor is never a letter key.
            a.tap(split+1,y);assert p.editor(a)['length']==len(expected)
            # A genuine long edge swipe still cancels the editor, without applying.
            move(a,[(3,y),(80,y),(200,y)]);assert not p.editor(a)['active'] and not a.diag()['draft_length']
            p.open_editor(a);p.layout(a,'zh26');k=keys(a)
            a.tap(3,k['a']['y']);a.tap(split-2,k['l']['y']);t.wait_for(lambda:p.editor(a)['preedit_length']==2)
            p.key(a,'enter');assert p.official(a)['text']=='al';capture(a,OUT/f'{w}-chinese.png')
            a.editor_action('cancel')
        finally:a.close()
    print(f'Edge keyboard {w}x{h}: A/L outer gaps, exact text, uppercase, drift/back, neighbor isolation, sticky cancel, multitouch and lifecycle passed.',flush=True)
if __name__=='__main__':
    for size in [(960,266),(936,280),(1280,360)]:run(*size)
