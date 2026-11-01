#!/usr/bin/env python3
"""Candidate disclosure stays relevant; stale queued clicks cannot cover text."""
import os,pathlib,signal,tempfile,time
import test_lvgl_app as t
from test_pinyin_ui import key,layout,open_editor,ready,editor
from image_gestures import capture

t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/keyboard-cleanup-0.2.17';OUT.mkdir(parents=True,exist_ok=True)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-candidates-') as tmp:
        a=t.Native(BINARY,pathlib.Path(tmp),official_adapter=False,screen={'width':w,'height':h,'direction':0})
        def shot(name):capture(a,OUT/f'{w}-{name}.png')
        def collapsed():
            s=editor(a);assert not s['expanded'] and 'expand' not in s['actions'],s
        def settle():t.wait_for(lambda:not editor(a)['pending'])
        def empty():
            while editor(a)['preedit_length']:key(a,'delete')
            settle();collapsed()
        def expand():
            ready(a);a.editor_action('expand');assert editor(a)['expanded']
        try:
            open_editor(a);collapsed();baseline=editor(a)['text_bounds'];shot('empty')
            # The former always-visible target is inert in an empty editor.
            a.tap(w-30,28);collapsed()
            layout(a,'en26');collapsed();assert editor(a)['text_bounds']==baseline;shot('english')
            layout(a,'zh26')
            for c in 'vvvv':key(a,c)
            settle();assert not editor(a)['candidate_count'];collapsed();shot('no-match');empty()
            # Catch the coalescing/query state without a fake candidate provider.
            k=next(k for k in editor(a)['keys'] if k['key']=='n')
            a.send('touch',x=k['x'],y=k['y'],pressed=True);a.send('touch',x=k['x'],y=k['y'],pressed=False)
            pending_seen=False
            deadline=time.monotonic()+3
            while time.monotonic()<deadline:
                s=editor(a)
                if s['pending'] and not s['candidate_count']:
                    assert 'expand' not in s['actions'] and not s['expanded'];pending_seen=True
                if s['candidate_count'] and not s['pending']:break
            ready(a);shot('candidates');expand();assert editor(a)['text_bounds']==baseline;shot('expanded')
            # Queue Delete followed by the old disclosure click before one UI tick.
            # SIGSTOP only pauses this isolated host fixture; no device is involved.
            s=editor(a);delete=next(k for k in s['keys'] if k['key']=='delete');b=s['actions']['expand'];x=(b[0]+b[2])//2;y=(b[1]+b[3])//2
            a.process.send_signal(signal.SIGSTOP);os.waitpid(a.process.pid,os.WUNTRACED)
            try:
                for px,py in [(delete['x'],delete['y']),(x,y)]:
                    a.send('touch',x=px,y=py,pressed=True);a.send('touch',x=px,y=py,pressed=False)
            finally:a.process.send_signal(signal.SIGCONT)
            t.wait_for(lambda:not editor(a)['preedit_length']);settle();collapsed();assert editor(a)['text_bounds']==baseline;shot('stale-click-rejected')
            for c in 'ni':key(a,c)
            expand();key(a,'language');collapsed();assert editor(a)['parked_count']==1
            layout(a,'zh26');ready(a);expand();key(a,'mode');collapsed();assert editor(a)['mode']==1
            key(a,'mode');collapsed();layout(a,'zh9')
            for c in '64426':key(a,'t9:'+c)
            expand();shot('nine-expanded');empty();shot('nine-empty')
            a.editor_action('cancel');a.open_settings();a.setting('models');a.page_action('添加 Provider');a.tap(250,120)
            t.wait_for(lambda:editor(a)['secret']);collapsed();shot('secret');a.tap(w-30,28);collapsed()
            assert any(c['bounds']==[w-110,6,w-67,49] for c in editor(a)['controls']), 'Password reveal target moved'
            collapsed()
        finally:a.close()
    print(f'Candidate disclosure {w}x{h}: empty/English/secret/no-match/query/valid/nine-key, queued stale click and stable editor geometry passed; query window observed={pending_seen}.',flush=True)

if __name__=='__main__':
    run(960,266);run(936,280)
