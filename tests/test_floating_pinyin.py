#!/usr/bin/env python3
"""Preedit floats around the intact candidate row in actual wide/portrait views."""
import pathlib,tempfile
import test_lvgl_app as ui
from test_pinyin_ui import BINARY,open_editor,editor,key,layout,ready,selected
from image_gestures import capture
ui.port=1
OUT=ui.ROOT/'.work/floating-pinyin';OUT.mkdir(parents=True,exist_ok=True)

def run(w,h,split=False):
    with tempfile.TemporaryDirectory(prefix='penchat-floating-pinyin-') as tmp:
        app=ui.Native(BINARY,pathlib.Path(tmp),official_adapter=False,screen={'width':w,'height':h})
        try:
            open_editor(app)
            if split:app.editor_action('compact-view')
            def tap(x,y):
                space=editor(app)['coordinate_space']
                app.tap(round(x*w/space['width']),round(y*h/space['height']))
            def press(name):
                item=next(k for k in editor(app)['keys'] if k['key']==name)
                tap(item['x'],item['y'])
            def select_layout(number):
                globe=next(k for k in editor(app)['keys'] if k['key']=='language')
                space=editor(app)['coordinate_space']
                app.long_press(round(globe['x']*w/space['width']),round(globe['y']*h/space['height']),.58)
                ui.wait_for(lambda:editor(app)['layout_menu'])
                app.editor_action('layout:'+str(number))
            e=editor(app);floating=not e['compact_full'];baseline=e['text_bounds'];panel=e['panel_top']
            assert e['composition_floating']==floating and not e['composition_visible'] and not e['composition_bounds']
            capture(app,OUT/f'{w}-idle.png')
            for c in 'nihao':press(c)
            ready(app);e=editor(app);b=e['composition_bounds'];toggle=e['actions']['expand']
            assert e['composition_floating']==floating and e['composition_visible'] and len(b)==4
            assert b[2]>b[0],(b,toggle)
            if floating:
                if h>w:assert b[3]<e['candidate_bounds'][1]
                else:assert b[1]>e['candidate_bounds'][3]
            else:assert b[1]==e['candidate_y']
            assert e['text_bounds']==baseline and e['panel_top']==panel
            if h>w:assert app.diag()['composer_bounds'][3]<b[1],(app.diag()['composer_bounds'],b)
            assert e['candidate_bounds'][0]==(b[0] if floating else b[2]+5)
            if h>w:assert toggle[2]-toggle[0]+1==64 and toggle[3]-toggle[1]+1==48
            capture(app,OUT/f'{w}-composing.png')
            tap((toggle[0]+toggle[2])//2,(toggle[1]+toggle[3])//2)
            e=editor(app);assert e['expanded']
            if split:
                assert e['compact_full'] and not e['composition_floating']
            else:
                assert e['actions']['expand']==toggle
                assert e['composition_bounds']==b and e['text_bounds']==baseline
            capture(app,OUT/f'{w}-expanded.png')
            if split:app.editor_action('compact-view')
            else:tap((toggle[0]+toggle[2])//2,(toggle[1]+toggle[3])//2)
            first=next(bounds for action,bounds in editor(app)['actions'].items() if action.startswith('choose:') and action.endswith(':0'))
            tap((first[0]+first[2])//2,(first[1]+first[3])//2);selected(app)
            assert not editor(app)['composition_bounds'] and editor(app)['length']>0
            capture(app,OUT/f'{w}-committed.png')
            if split:app.editor_action('compact-view')
            select_layout(1);press('a')
            assert not editor(app)['composition_bounds']
            select_layout(2)
            for n in '64426':press('t9:'+n)
            ready(app);assert editor(app)['composition_bounds']
            capture(app,OUT/f'{w}-nine.png')
            for _ in range(5):press('delete')
            ui.wait_for(lambda:not editor(app)['pending'])
            assert not editor(app)['composition_bounds']
            print('Floating pinyin: idle/compose/expand/commit/English/nine-key and stable geometry:',w,h,'passed',flush=True)
        finally:app.close()

if __name__=='__main__':
    for size in [(568,1210),(600,1280),(960,266),(640,260,True)]:run(*size)
