#!/usr/bin/env python3
"""Two physical contact IDs may overlap while typing, without cancelling keys."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
import test_pinyin_ui as p
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
def run(width,height):
    with tempfile.TemporaryDirectory(prefix='penchat-key-rollover-') as folder:
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height})
        try:
            d=a.diag();a.tap(250 if height>width else 500,d['toolbar_y']+30 if height>width else 232)
            t.wait_for(lambda:bool(p.editor(a)['keys']));p.layout(a,'en26')
            k={v['key']:(v['x'],v['y']) for v in p.editor(a)['keys']}
            def frame(entries,cancel=False):
                points=[{'id':ident,'x':xy[0],'y':xy[1]} for ident,xy in entries]
                x,y=entries[0][1] if entries else (0,0)
                a.send('touch',x=x,y=y,pressed=bool(entries),cancelled=cancel,points=points)
            expected=''
            # Both release orders, point-array reordering, alternating slot IDs.
            # The old global multi-finger guard loses both letters in every pair.
            for i in range(60):
                a_id,b_id=f'0:{i*2}',f'1:{i*2+1}'
                frame([(a_id,k['a'])]);frame([(b_id,k['l']),(a_id,k['a'])])
                if i%2:frame([(a_id,k['a'])])
                else:frame([(b_id,k['l'])])
                frame([]);expected+='al'
            t.wait_for(lambda:p.editor(a)['length']==len(expected))
            assert p.official(a)['text']==expected
            # Once a finger drifts out of its key it cannot reactivate on return;
            # the other independent contact still commits exactly once.
            frame([('a',k['a'])]);frame([('a',k['a']),('b',k['l'])])
            frame([('a',k['s']),('b',k['l'])]);frame([('a',k['a']),('b',k['l'])]);frame([])
            expected+='l';t.wait_for(lambda:p.editor(a)['length']==len(expected))
            # Duplicate frames do not repeat input. Key + text, three contacts,
            # cancellation and background continue to cancel without committing.
            for scenario in ['text','three','cancel','background']:
                frame([('a',k['a'])]);frame([('a',k['a'])])
                if scenario=='text':
                    bounds=p.editor(a)['text_bounds'];xy=((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
                    frame([('a',k['a']),('b',xy)])
                elif scenario=='three':frame([('a',k['a']),('b',k['l']),('c',k['s'])])
                elif scenario=='cancel':frame([('a',k['a'])],True)
                else:a.send('visibility',visible=False);a.send('visibility',visible=True)
                frame([],True);frame([]);time.sleep(.06)
                assert p.editor(a)['length']==len(expected),scenario
            # Ordinary single-contact packets and Shift retain the same semantics.
            for name in ['shift','a','l']:
                keys={v['key'].lower():(v['x'],v['y']) for v in p.editor(a)['keys']}
                frame([('single',keys[name])]);frame([]);time.sleep(.04)
            expected+='Al';t.wait_for(lambda:p.editor(a)['length']==len(expected))
            assert p.official(a)['text']==expected
            # Native-contact long press opens the globe selector while held;
            # held delete repeats, and its final release never adds an extra tap.
            frame([('globe',k['language'])]);time.sleep(.82)
            t.wait_for(lambda:p.editor(a)['layout_menu'])
            frame([]);assert p.editor(a)['layout_menu']
            a.editor_action('layout:1')
            t.wait_for(lambda:not p.editor(a)['layout_menu'])
            before=p.editor(a)['length'];frame([('delete',k['delete'])]);time.sleep(.72)
            t.wait_for(lambda:p.editor(a)['length']<before)
            frame([]);time.sleep(.06)
            removed=before-p.editor(a)['length'];expected=expected[:-removed]
            assert p.official(a)['text']==expected
            # Chinese rollover keeps original input sequence, independently of IME.
            p.layout(a,'zh26');k={v['key']:(v['x'],v['y']) for v in p.editor(a)['keys']}
            for i in range(20):
                frame([('n'+str(i),k['n'])]);frame([('n'+str(i),k['n']),('i'+str(i),k['i'])]);frame([('i'+str(i),k['i'])]);frame([])
            t.wait_for(lambda:p.editor(a)['preedit_length']==40)
            p.key(a,'enter');t.wait_for(lambda:not p.editor(a)['preedit_length'])
            assert p.official(a)['text']==expected+'ni'*20
            print(f'Keyboard rollover {width}x{height}: 120 English + 40 Chinese overlapped keys, release order, IDs, drift, cancellation and lifecycle passed.',flush=True)
        finally:a.close()
if __name__=='__main__':
    for size in [(960,266),(568,1210)]:run(*size)
