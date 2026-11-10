#!/usr/bin/env python3
"""Portrait disclosure replaces key caps and keeps the toggle/text fixed."""
import pathlib,tempfile
import test_lvgl_app as ui
from test_pinyin_ui import BINARY,key,layout,open_editor,ready,editor,selected
from image_gestures import capture
OUT=ui.ROOT/'.work/portrait-candidates';OUT.mkdir(parents=True,exist_ok=True)
ui.port=1

def tap(app,b):app.tap((b[0]+b[2])//2,(b[1]+b[3])//2)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-portrait-candidates-') as tmp:
        app=ui.Native(BINARY,pathlib.Path(tmp),official_adapter=False,screen={'width':w,'height':h,'direction':0})
        try:
            open_editor(app);layout(app,'zh26')
            for c in 'nihao':key(app,c)
            ready(app);before=editor(app);button=before['actions']['expand'];text=before['text_bounds']
            first_key=min(k['bounds'][1] for k in before['keys'] if k['key']!='language')
            capture(app,OUT/f'{w}-collapsed.png')
            tap(app,button);e=editor(app)
            assert e['expanded'] and e['actions']['expand']==button,e
            assert e['text_bounds']==text,(text,e['text_bounds'])
            assert e['candidate_bounds'][1]>=first_key,(first_key,e['candidate_bounds'])
            assert {k['key'] for k in e['keys']}=={'language'},e['keys']
            assert e['candidate_bounds'][1]>button[3]
            capture(app,OUT/f'{w}-expanded.png')
            # Both directions use the same actual widget, rather than a queued action.
            tap(app,button);e=editor(app)
            assert not e['expanded'] and e['actions']['expand']==button
            assert any(k['key']=='q' for k in e['keys']) and e['preedit_length']==5
            tap(app,button);e=editor(app)
            choice=next(b for name,b in e['actions'].items() if name.startswith('choose:') and b[1]>=e['candidate_bounds'][1] and b[3]<=e['candidate_bounds'][3])
            tap(app,choice);selected(app)
            assert not editor(app)['expanded'] and editor(app)['length']>0
            assert any(k['key']=='q' for k in editor(app)['keys'])
            # Nine-key uses the same replacement panel and fixed toggle.
            layout(app,'zh9')
            for n in '64426':key(app,'t9:'+n)
            ready(app);button=editor(app)['actions']['expand'];text=editor(app)['text_bounds']
            tap(app,button);e=editor(app)
            assert e['expanded'] and e['actions']['expand']==button and e['text_bounds']==text
            assert {k['key'] for k in e['keys']}=={'language'}
            assert not any(name.startswith('exact:') for name in e['actions'])
            capture(app,OUT/f'{w}-nine-expanded.png')
            tap(app,button);assert any(k['key']=='t9:2' for k in editor(app)['keys'])
            print('Portrait candidate key replacement, fixed toggle, selection and nine-key:',w,h,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(568,1210),(600,1280)]:run(*size)
