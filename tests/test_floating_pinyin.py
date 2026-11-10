#!/usr/bin/env python3
"""Portrait preedit is a content-sized floating label, never a permanent strip."""
import pathlib,tempfile
import test_lvgl_app as ui
from test_pinyin_ui import BINARY,open_editor,editor,key,layout,ready,selected
from image_gestures import capture
ui.port=1
OUT=ui.ROOT/'.work/floating-pinyin';OUT.mkdir(parents=True,exist_ok=True)

def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-floating-pinyin-') as tmp:
        app=ui.Native(BINARY,pathlib.Path(tmp),official_adapter=False,screen={'width':w,'height':h})
        try:
            open_editor(app);e=editor(app);baseline=e['text_bounds'];panel=e['panel_top']
            assert e['composition_floating'] and not e['composition_bounds']
            capture(app,OUT/f'{w}-idle.png')
            for c in 'nihao':key(app,c)
            ready(app);e=editor(app);b=e['composition_bounds'];toggle=e['actions']['expand']
            assert e['composition_floating'] and len(b)==4
            assert b[2]-b[0]<w//2 and b[3]+1==e['candidate_y'],(b,toggle)
            assert e['text_bounds']==baseline and e['panel_top']==panel
            assert app.diag()['composer_bounds'][3]+1==b[1],(app.diag()['composer_bounds'],b)
            assert e['candidate_bounds'][0]==0
            assert toggle[2]-toggle[0]+1==64 and toggle[3]-toggle[1]+1==48
            capture(app,OUT/f'{w}-composing.png')
            app.tap(toggle[2]-2,(toggle[1]+toggle[3])//2)
            e=editor(app);assert e['expanded'] and e['actions']['expand']==toggle
            assert e['composition_bounds']==b and e['text_bounds']==baseline
            capture(app,OUT/f'{w}-expanded.png')
            app.tap(toggle[2]-2,(toggle[1]+toggle[3])//2)
            key(app,'space');selected(app)
            assert not editor(app)['composition_bounds'] and editor(app)['length']>0
            capture(app,OUT/f'{w}-committed.png')
            layout(app,'en26');key(app,'a')
            assert not editor(app)['composition_bounds']
            layout(app,'zh9')
            for n in '64426':key(app,'t9:'+n)
            ready(app);assert editor(app)['composition_bounds']
            capture(app,OUT/f'{w}-nine.png')
            for _ in range(5):key(app,'delete')
            ui.wait_for(lambda:not editor(app)['pending'])
            assert not editor(app)['composition_bounds']
            print('Floating pinyin: idle/compose/expand/commit/English/nine-key and stable geometry:',w,h,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(568,1210),(600,1280)]:run(*size)
