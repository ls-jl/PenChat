#!/usr/bin/env python3
"""Actual editor: typo candidates preserve raw input, selection and learning semantics."""
import json,pathlib,tempfile,time
import test_pinyin_ui as p
import test_lvgl_app as t
from image_gestures import capture
OUT=t.ROOT/'.work/pinyin-correction';OUT.mkdir(parents=True,exist_ok=True)

def type_raw(a,raw):
    for c in raw:p.key(a,c)
    p.ready(a)

def first(a):
    s=p.editor(a);return f'choose:{s["revision"]}:0'

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-correction-') as directory:
        root=pathlib.Path(directory);profile=root/'ime/user.json'
        a=t.Native(p.BINARY,root,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            p.open_editor(a);type_raw(a,'niao');s=p.editor(a)
            assert 0<s['corrected_count']<=2
            capture(a,OUT/f'{w}-valid-alternatives.png')
            a.editor_action(f"choose:{s['revision']}:3");p.selected(a)
            assert p.official(a)['text']=='你好'
            a.editor_action('cancel');assert not profile.exists()
            p.open_editor(a);type_raw(a,'niao');p.key(a,'space');p.selected(a)
            assert p.official(a)['text']=='鸟','Normal first choice must remain unchanged'
            a.editor_action('cancel');assert not profile.exists()
            p.open_editor(a);type_raw(a,'nihoa');s=p.editor(a)
            assert s['corrected_count']>0 and s['preedit_length']==5 and s['length']==0
            capture(a,OUT/f'{w}-candidates.png')
            b=s['actions'][first(a)];a.long_press((b[0]+b[2])//2,(b[1]+b[3])//2)
            assert p.editor(a)['length']==0 and p.editor(a)['preedit_length']==5,'Long press must not select'
            capture(a,OUT/f'{w}-explanation.png')
            assert p.official(a)['text']=='nihoa','Correction must never rewrite raw composition'
            p.ready(a)
            a.editor_action(first(a));p.selected(a)
            assert p.official(a)['text']=='你好'
            a.editor_action('cancel');assert not profile.exists()
            p.open_editor(a);type_raw(a,'nihoa');p.key(a,'enter')
            assert p.official(a)['text']=='nihoa'
            a.editor_action('cancel');assert not profile.exists()
            p.open_editor(a);type_raw(a,'nihoaq');a.editor_action(first(a));t.wait_for(lambda:p.editor(a)['preedit_length']==1)
            assert p.official(a)['text']=='你好q'
            a.editor_action('cancel');assert not profile.exists()
            p.open_editor(a);type_raw(a,'nihoa');a.editor_action(first(a));p.selected(a);a.editor_action('done')
            t.wait_for(profile.exists);records=json.loads(profile.read_text())['records']
            assert any(r['word']=='你好' and r['pinyin']=="ni'hao" for r in records)
            assert not any(r['pinyin']=='nihoa' for r in records)
            a.open_settings();a.setting('keyboard');s=a.diag()['settings']['rows']['pinyin_correction'];assert s['checked']
            capture(a,OUT/f'{w}-settings-on.png');b=s['bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            assert not a.diag()['settings']['rows']['pinyin_correction']['checked']
            capture(a,OUT/f'{w}-settings-off.png')
            a.to_chat();p.open_editor(a);type_raw(a,'nihoa');assert p.editor(a)['corrected_count']==0
            a.editor_action('cancel')
        finally:a.close()
        a=t.Native(p.BINARY,root,official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            a.open_settings();a.setting('keyboard');assert not a.diag()['settings']['rows']['pinyin_correction']['checked']
            a.to_chat();p.open_editor(a);type_raw(a,'nihoa');assert p.editor(a)['corrected_count']==0
        finally:a.close()
    print(f'Pinyin correction UI {w}x{h}: marker/hint, raw return, partial consumption, cancel/confirm learning and saved switch passed.',flush=True)

if __name__=='__main__':
    for size in [(960,266),(936,280)]:run(*size)
