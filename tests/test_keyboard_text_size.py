#!/usr/bin/env python3
"""Real LVGL font scaling preserves every key and measured caption at each view."""
import json,os,pathlib,sqlite3,tempfile
import test_lvgl_app as ui
from test_pinyin_ui import open_editor,editor
ui.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ui.ROOT/'build/host/penchat-lvgl'))

def run(w,h,split):
    states={}
    for percent in (100,150,75):
        with tempfile.TemporaryDirectory(prefix='penchat-keyboard-text-') as tmp:
            p=pathlib.Path(tmp)
            schema=(ui.ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
            with sqlite3.connect(p/'penchat.db') as db:
                db.executescript(schema)
                db.execute("insert into app_settings values('reading_font_percent',?,1)",(json.dumps(percent),))
            app=ui.Native(BINARY,p,seed_model=False,official_adapter=False,screen={'width':w,'height':h})
            try:
                open_editor(app)
                if split:app.editor_action('compact-view')
                def press(name):
                    state=editor(app);item=next(k for k in state['keys'] if k['key']==name);space=state['coordinate_space']
                    app.tap(round(item['x']*w/space['width']),round(item['y']*h/space['height']))
                def choose(number):
                    state=editor(app);globe=next(k for k in state['keys'] if k['key']=='language');space=state['coordinate_space']
                    app.long_press(round(globe['x']*w/space['width']),round(globe['y']*h/space['height']),.58)
                    ui.wait_for(lambda:editor(app)['layout_menu']);app.editor_action('layout:'+str(number))
                state=editor(app);states[percent]=state
                baseline=[(k['key'],k['bounds'],k['hit_bounds']) for k in state['keys']]
                if percent!=100:
                    assert baseline==[(k['key'],k['bounds'],k['hit_bounds']) for k in states[100]['keys']]
                for mode in ('layout:0','mode','symbols','mode','layout:2'):
                    if mode.startswith('layout:'):choose(int(mode[-1]))
                    else:press(mode)
                    for key in editor(app)['keys']:
                        if 'ink_width' in key:
                            x1,y1,x2,y2=key['bounds']
                            assert key['ink_width']<=x2-x1 and key['font_height']<=y2-y1,(w,h,split,percent,key)
                for role in ('candidate','composition','menu'):
                    assert state['font_sizes'][role]==(states[100]['font_sizes'][role]*percent+50)//100
            finally:app.close()
    if h>w:assert states[150]['font_sizes']['character']>states[100]['font_sizes']['character']
    print('KEYBOARD_TEXT_SIZE',w,h,split,'passed',flush=True)

if __name__=='__main__':
    for fixture in ((960,266,False),(640,260,False),(640,260,True),(568,1210,False)):run(*fixture)
