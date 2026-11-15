#!/usr/bin/env python3
"""Enum settings use small anchored popups in all three LVGL layouts."""
import json,pathlib,sqlite3,tempfile,time
import test_lvgl_app as ui
from test_reasoning_ui import seed
from settings_ui import locate
from provider_ui import activate,capture
from choice_ui import select,dismiss
ui.port=1
OUT=ui.ROOT/'.work/choice-menus';OUT.mkdir(parents=True,exist_ok=True)

def menu(a,kind,w,h):
    d=a.diag()['context_menu'];assert d['kind']=='choice:'+kind,d
    b=d['bounds'];assert 0<=b[0]<b[2]<w and 0<=b[1]<b[3]<h,b
    assert (b[2]-b[0]+1)*(b[3]-b[1]+1)<w*h*.7,b
    assert sum(r['selected'] for r in d['choices'].values())==1,d
    for row in d['choices'].values():assert row['bounds'][3]-row['bounds'][1]+1>=44
    # A scroll that leaves the panel must retain ownership until release.
    x=(b[0]+b[2])//2;y=(b[1]+b[3])//2
    a.send('touch',x=x,y=y,pressed=True)
    for step in range(1,6):
        a.send('touch',x=x+(1-x)*step//5,y=y,pressed=True);time.sleep(.025)
    a.send('touch',x=1,y=y,pressed=False);time.sleep(.05)
    assert a.diag()['context_menu']['kind']=='choice:'+kind,('Drag-out closed the menu',kind,a.diag()['context_menu'])
    # A gesture beginning on the backdrop is not a tap after movement/cancel.
    a.send('touch',x=1,y=2,pressed=True)
    a.send('touch',x=1,y=40,pressed=True)
    a.send('touch',x=1,y=40,pressed=False)
    assert a.diag()['context_menu']['kind']=='choice:'+kind
    a.send('touch',x=1,y=2,pressed=True)
    a.send('touch',x=1,y=2,pressed=False,cancelled=True)
    assert a.diag()['context_menu']['kind']=='choice:'+kind
    capture(a,OUT,f'{w}-{kind}')
    return d

def chat(a):
    # Portrait headings scroll with the form. Reveal navigation before tapping it.
    for _ in range(24):
        d=a.diag()
        if d['context_menu']['kind']=='choice:provider_exit':select(a,'discard');continue
        if d['view']=='chat':return
        if d['height']>d['width'] and d['scroll_y']>0:
            a.settings_swipe(min(500,d['height']//2));time.sleep(.2)
        else:a.tap(48,48) if d['height']>d['width'] else a.tap(40,32)
    raise AssertionError('Cannot return to chat')

for w,h in [(640,260),(960,266),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-options-') as folder:
        p=pathlib.Path(folder);seed(p,1)
        a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',p,screen={'width':w,'height':h,'direction':0})
        def stored(key):
            with sqlite3.connect(p/'penchat.db') as db:return json.loads(db.execute('select value_json from app_settings where key=?',(key,)).fetchone()[0])
        try:
            a.open_settings()
            for row,kind,key,value in [('reasoning','reasoning_display','reasoning_display','during'),
                ('history_time','history_time','history_time_display','off'),
                ('message_edit','message_edit','message_edit_default','archive'),
                ('composer_shortcut','composer_shortcut','landscape_composer_shortcut','manual'),
                ('web_policy','new_chat_web','new_chat_web','recent')]:
                locate(a.diag,a.settings_swipe,row);before=a.diag()['scroll_y']
                a.setting(row);menu(a,kind,w,h);dismiss(a)
                assert a.diag()['view']=='settings' and a.diag()['scroll_y']==before
                a.setting(row);select(a,value);assert stored(key)==value
                assert a.diag()['scroll_y']==before
                a.setting(row);assert menu(a,kind,w,h)['choices'][value]['selected'];dismiss(a)
            a.setting('models');activate(a,'provider-open:p')
            activate(a,'provider-protocol');menu(a,'protocol',w,h);select(a,'chat')
            assert a.diag()['provider_protocol']=='chat'
            activate(a,'balance_vendor');menu(a,'balance_vendor',w,h);select(a,'deepseek')
            activate(a,'provider_advanced');activate(a,'model_adapter')
            menu(a,'model_adapter',w,h);select(a,'minimax') # Last row requires local scrolling.
            activate(a,'provider_effort');menu(a,'provider_effort',w,h);select(a,'max')
            # All Provider edits are still in the unsaved form.
            with sqlite3.connect(p/'penchat.db') as db:
                meta=json.loads(db.execute("select metadata_json from providers where id='p'").fetchone()[0]);assert meta['api']=='responses'
            chat(a);a.open_settings();a.setting('presets');activate(a,'新增预设')
            # New preset's prompt-mode control, measured by the native page.
            activate(a,'preset:系统提示词');menu(a,'preset_prompt_mode',w,h);select(a,'clear')
            activate(a,'preset:模型');menu(a,'preset_model_id',w,h);select(a,'a')
            activate(a,'preset:推理强度');menu(a,'preset_effort',w,h);select(a,'api_default')
            activate(a,'preset:上下文压缩模型');menu(a,'preset_compression_model_id',w,h);dismiss(a)
        finally:a.close()
        print('Option menus passed:',w,h,flush=True)
print('THREE_LAYOUT_ENUM_POPUPS_BACKDROP_SELECTION_LOCAL_SCROLL_UNSAVED_FORMS_OK')
