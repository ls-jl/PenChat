#!/usr/bin/env python3
"""Default official keyboard preference through real LVGL and durable settings."""
import json,pathlib,sqlite3,tempfile,time
import test_lvgl_app as ui
from test_pinyin_ui import BINARY,open_editor,editor,key
from provider_ui import activate
from ui_scale_ui import back
from settings_ui import locate
ui.port=1
KEY='prefer_official_keyboard'
def preference(a):return editor(a)['official_preference']
def close_native(a):a.editor_action('cancel' if 'cancel' in editor(a)['actions'] else 'done')
def settings(a):a.open_settings();a.setting('keyboard')
def cancel_official(a,request,error=''):
    a.send('keyboard.result',requestId=request['requestId'],confirmed=False,error=error)
    ui.wait_for(lambda:not a.diag()['keyboard'])
    ui.wait_for(lambda:bool(editor(a)['keys']))
    time.sleep(.15);assert not a.diag()['keyboard']
def check_official(a,role=None):
    request=a.receive('keyboard.open')
    assert a.diag()['keyboard'] and not editor(a)['ocr_active']
    if role and a.diag()['height']>a.diag()['width']:
        assert request['field']['fontSize']==a.diag()['reading_font']['roles'][role]
    assert request['field']['fontSize']>0 and request['field']['fontLineHeight']>0
    assert request['field']['width']>0 and request['field']['height']>0,request
    if a.diag()['height']>a.diag()['width']:
        a.send('keyboard.layout',requestId=request['requestId'],height=a.diag()['height']//2,foreground=True)
        field=a.receive('keyboard.field')
        while field['requestId']!=request['requestId']:field=a.receive('keyboard.field')
        assert field['fontSize']==request['field']['fontSize'] and field['fontLineHeight']>0
        state=editor(a);assert state['text_visible'] and state['text_in_target']
        a.send('keyboard.change',requestId=request['requestId'],text='mirror',cursorIndex=6)
        assert editor(a)['length']==6
    cancel_official(a,request)
    key(a,'language');assert not a.diag()['keyboard']
    a.editor_action('official');manual=a.receive('keyboard.open');assert manual['requestId']!=request['requestId']
    cancel_official(a,manual,'fixture open failure')
    a.send('window.focus',focused=False,sequence=10);a.send('window.focus',focused=True,sequence=11)
    time.sleep(.15);assert not a.diag()['keyboard']
    close_native(a)
def run(w,h):
    with tempfile.TemporaryDirectory(prefix='penchat-official-default-') as folder:
        p=pathlib.Path(folder);kwargs=dict(official_adapter=False,screen={'width':w,'height':h})
        a=ui.Native(BINARY,p,**kwargs)
        try:
            assert not preference(a)['saved'];open_editor(a);assert not a.diag()['keyboard'];close_native(a)
            settings(a)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("create trigger reject_official before insert on app_settings when NEW.key='prefer_official_keyboard' begin select raise(abort,'fixture failure'); end")
            a.setting(KEY);ui.wait_for(lambda:preference(a)['error'])
            assert not preference(a)['saved'] and not preference(a)['pending']
            with sqlite3.connect(p/'penchat.db') as db:db.execute('drop trigger reject_official')
            row=locate(a.diag,a.settings_swipe,KEY);x1,y1,x2,y2=row['bounds']
            lock=sqlite3.connect(p/'penchat.db');lock.execute('begin immediate')
            try:
                for _ in range(2):
                    a.send('touch',x=(x1+x2)//2,y=(y1+y2)//2,pressed=True);time.sleep(.07)
                    a.send('touch',x=(x1+x2)//2,y=(y1+y2)//2,pressed=False);time.sleep(.10)
            finally:lock.rollback();lock.close()
            ui.wait_for(lambda:preference(a)['saved'] and not preference(a)['pending'])
            with sqlite3.connect(p/'penchat.db') as db:assert json.loads(db.execute('select value_json from app_settings where key=?',(KEY,)).fetchone()[0]) is True
            back(a);assert a.diag()['view']=='settings';back(a);assert a.diag()['view']=='chat';open_editor(a);check_official(a,'composer')
            a.open_settings();a.setting('models');activate(a,'provider-open:fixture-provider');activate(a,'field:name');check_official(a,'field_value')
            activate(a,'field:apiKey');request=a.receive('keyboard.open');assert request['sensitive'];cancel_official(a,request);close_native(a)
        finally:a.close()
        a=ui.Native(BINARY,p,**kwargs)
        try:
            assert preference(a)['saved'];open_editor(a);check_official(a,'composer')
            settings(a);a.setting(KEY);ui.wait_for(lambda:not preference(a)['saved'] and not preference(a)['pending'])
        finally:a.close()
        for invalid in ['true',1,None,{},[]]:
            with sqlite3.connect(p/'penchat.db') as db:db.execute('update app_settings set value_json=? where key=?',(json.dumps(invalid),KEY))
            a=ui.Native(BINARY,p,**kwargs)
            try:
                assert not preference(a)['saved'];open_editor(a);assert not a.diag()['keyboard'];close_native(a)
            finally:a.close()
        print('Official default setting: default, durable toggle, failed save/retry, chat/form/secret routes, cancellation/native/manual, restart and invalid values:',w,h,'passed',flush=True)
def selection_only():
    from test_message_edit import fixture,menu,action
    with tempfile.TemporaryDirectory(prefix='penchat-official-select-') as folder:
        p=pathlib.Path(folder);fixture(p,1)
        with sqlite3.connect(p/'penchat.db') as db:db.execute('insert into app_settings values(?,?,1)',(KEY,'true'))
        a=ui.Native(BINARY,p,official_adapter=False)
        try:
            menu(a);action(a,'select');ui.wait_for(lambda:editor(a)['selection_only'])
            assert not a.diag()['keyboard'] and not editor(a)['ocr_active'] and not editor(a)['keys']
            close_native(a)
        finally:a.close()
        print('Official default preserves read-only selection session',flush=True)
if __name__=='__main__':
    for size in [(960,266),(640,260),(568,1210)]:run(*size)
    selection_only()
