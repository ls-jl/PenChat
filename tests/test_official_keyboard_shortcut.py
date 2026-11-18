#!/usr/bin/env python3
"""Real LVGL: opt-in legacy-position system IME entry and async persistence."""
import json,pathlib,sqlite3,tempfile,time
import test_lvgl_app as ui
from image_gestures import capture
from settings_ui import locate
ui.port=1
KEY='quick_official_keyboard'
OUT=ui.ROOT/'.work/official-shortcut';OUT.mkdir(parents=True,exist_ok=True)
def state(a):return a.diag()['editor']
def preference(a):return state(a)['official_shortcut']
def settings(a):a.open_settings();a.setting('keyboard')
def toggle(a,value):
    a.setting(KEY);ui.wait_for(lambda:preference(a)['saved']==value and not preference(a)['pending'])
def menu(a):
    k=next(k for k in state(a)['keys'] if k['key']=='language')
    space=state(a)['coordinate_space'];x,y=k['x'],k['y']
    if space['scaled']:x,y=round(x*space['native_width']/space['width']),round(y*space['native_height']/space['height'])
    a.long_press(x,y,.58);ui.wait_for(lambda:state(a)['layout_menu'])
def open_editor(a):
    b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    ui.wait_for(lambda:state(a)['active'] and bool(state(a)['keys']))
    ui.wait_for(lambda:not a.diag().get('surface_motion',False))
def native_close(a):a.editor_action('done')
def run(w,h,scale=100):
    with tempfile.TemporaryDirectory(prefix='penchat-official-shortcut-') as tmp:
        p=pathlib.Path(tmp);options=dict(official_adapter=False,screen={'width':w,'height':h},ui_scale_percent=scale)
        a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',p,**options)
        try:
            assert not preference(a)['saved'];open_editor(a)
            assert 'official' not in state(a)['actions']
            menu(a);assert 'official' in state(a)['actions']
            a.editor_action('layout:0');native_close(a)
            settings(a)
            if (w,h,scale)==(960,266,100):
                with sqlite3.connect(p/'penchat.db') as db:
                    db.execute("create trigger reject_shortcut before insert on app_settings when NEW.key='quick_official_keyboard' begin select raise(abort,'fixture failure'); end")
                a.setting(KEY);ui.wait_for(lambda:preference(a)['error'])
                assert not preference(a)['saved'] and not preference(a)['pending']
                with sqlite3.connect(p/'penchat.db') as db:db.execute('drop trigger reject_shortcut')
            if (w,h,scale)==(960,266,100):
                b=locate(a.diag,a.settings_swipe,KEY)['bounds']
                lock=sqlite3.connect(p/'penchat.db');lock.execute('begin immediate')
                try:
                    for _ in range(2):
                        a.send('touch',x=(b[0]+b[2])//2,y=(b[1]+b[3])//2,pressed=True);time.sleep(.07)
                        a.send('touch',x=(b[0]+b[2])//2,y=(b[1]+b[3])//2,pressed=False);time.sleep(.10)
                finally:lock.rollback();lock.close()
                ui.wait_for(lambda:preference(a)['saved'] and not preference(a)['pending'])
            else:toggle(a,True)
            assert not state(a)['official_preference']['saved']
            capture(a,OUT/f'{w}-{h}-{scale}-settings.png')
            a.to_chat();open_editor(a)
            actions=state(a)['actions'];assert 'official' in actions and not state(a)['official']
            keys=state(a)['keys'];capture(a,OUT/f'{w}-{h}-{scale}-shortcut.png')
            if h<w and not state(a)['compact_full']:
                assert actions['official'][0]>actions['cancel'][2]
                assert actions['official'][2]<actions['ocr-mode'][0]
            menu(a)
            # The one remaining official action belongs to the fixed function row.
            assert state(a)['actions']['official']==actions['official']
            assert not (state(a)['layout_popup'][1]<=actions['official'][1]<=state(a)['layout_popup'][3])
            a.editor_action('layout:0');assert state(a)['keys']==keys
            a.editor_action('official');request=a.receive('keyboard.open')
            a.send('keyboard.result',requestId=request['requestId'],confirmed=not request['inline'],retained=request['inline'],text='voice draft')
            ui.wait_for(lambda:not state(a)['active'])
            assert a.diag()['draft_length']==11 and not a.diag()['composer_hidden']
        finally:a.close()
        a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',p,**options)
        try:
            assert preference(a)['saved']
            settings(a);toggle(a,False);a.to_chat();open_editor(a)
            assert 'official' not in state(a)['actions'];menu(a);assert 'official' in state(a)['actions']
            a.editor_action('layout:0');native_close(a)
        finally:a.close()
        if (w,h,scale)==(960,266,100):
            for invalid in ['true',1,None,{},[]]:
                with sqlite3.connect(p/'penchat.db') as db:db.execute('update app_settings set value_json=? where key=?',(json.dumps(invalid),KEY))
                a=ui.Native(ui.ROOT/'build/host/penchat-lvgl',p,**options)
                try:assert not preference(a)['saved']
                finally:a.close()
        print('Official shortcut: default/menu, opt-in/direct, independent preference, retained draft, restart/off:',w,h,scale,'passed',flush=True)
if __name__=='__main__':
    for fixture in [(960,266,100),(640,260,100),(568,1210,100),(936,280,150),(568,1210,150)]:run(*fixture)
