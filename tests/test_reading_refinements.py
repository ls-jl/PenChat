#!/usr/bin/env python3
"""Focused image popup and grouped reading gestures. Private fixtures, no real API/device."""
import json,os,pathlib,sqlite3,tempfile,time
import test_lvgl_app as t
from test_text_selection import fixture,ro,press
from test_photo_editor import png
from image_gestures import capture
OUT=t.ROOT/'.work/reading-refinements';OUT.mkdir(parents=True,exist_ok=True)
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
t.port=1

def tap(a,b):
    if isinstance(b,dict):b=b['bounds']
    a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def copy_selection(a):
    b=ro(a)['menu_bounds'];a.tap(b[0]+(b[2]-b[0])*3//4,(b[1]+b[3])//2)

def image_fixture(p):
    fixture(p);original=p/'original.png';png(original,60,80)
    (p/'assets').mkdir();copies=[]
    with sqlite3.connect(p/'penchat.db') as db:
        for i in range(2):
            mid='photo'+str(i);path=p/'assets'/(mid+'.png');path.write_bytes(original.read_bytes());copies.append(path)
            db.execute("INSERT INTO chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) VALUES(?,'s',?,'user','complete','image','',1,1)",(mid,3+i))
            db.execute("INSERT INTO message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,created_at) VALUES(?,'s',?,'image','album',?,?,1)",(mid,mid,str(original),str(path)))
            db.execute("INSERT INTO message_parts(id,session_id,message_id,part_index,part_type,asset_id) VALUES(?,'s',?,0,'image',?)",(mid,mid,mid))
    return original,copies

def image_check(w,h,mode,scale):
    with tempfile.TemporaryDirectory(prefix='penchat-image-popup-') as folder:
        p=pathlib.Path(folder);original,copies=image_fixture(p)
        with sqlite3.connect(p/'penchat.db') as db:db.execute("INSERT INTO app_settings VALUES('theme_mode',?,1)",(json.dumps(mode),))
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0},ui_scale_percent=scale)
        try:
            t.wait_for(lambda:len(a.diag()['chat_images'])==2)
            time.sleep(.2)
            photo=sorted(a.diag()['chat_images'],key=lambda x:x['x'])[0]
            x=photo['x']+photo['width']//2;y=photo['y']+photo['height']//2
            a.long_press(x,y,.66)
            t.wait_for(lambda:a.diag()['context_menu']['kind']=='image-delete')
            state=a.diag();b=state['context_menu']['choices']['delete']['bounds'];panel=state['context_menu']['bounds']
            assert b[2]-b[0]+1>=80*scale//100 and b[3]-b[1]+1>=44
            assert panel[3]-panel[1]+1<h//2 and not state['composer_hidden']
            time.sleep(.18);capture(a,OUT/f'image-{w}-{mode}-{scale}.png')
            # One tap outside dismisses without sending or opening the viewer.
            a.tap(w-3,h//2);t.wait_for(lambda:not a.diag()['modal'])
            assert copies[0].exists() and copies[1].exists()
            a.long_press(x,y,.66);t.wait_for(lambda:a.diag()['context_menu']['kind']=='image-delete')
            tap(a,a.diag()['context_menu']['choices']['delete'])
            t.wait_for(lambda:not copies[0].exists())
            assert copies[1].exists() and original.exists()
            assert not a.diag()['modal']
            # A drag on the next popup hands the gesture back to the reader.
            photo=a.diag()['chat_images'][0];x=photo['x']+photo['width']//2;y=photo['y']+photo['height']//2
            a.long_press(x,y,.66);t.wait_for(lambda:a.diag()['context_menu']['kind']=='image-delete')
            press(a,w-12,h//2);press(a,w-12,h//2-25);press(a,w-12,h//2-50,False)
            t.wait_for(lambda:not a.diag()['modal']);assert copies[1].exists()
        finally:a.close()
    print('Image popup',w,h,mode,scale,'passed',flush=True)

def reading_check(w,h,mode="dark",scale=100):
    with tempfile.TemporaryDirectory(prefix='penchat-read-group-') as folder:
        p=pathlib.Path(folder);fixture(p)
        with sqlite3.connect(p/'penchat.db') as db:db.execute("INSERT INTO app_settings VALUES('theme_mode',?,1)",(json.dumps(mode),))
        a=t.Native(BINARY,p,official_adapter=False,screen={'width':w,'height':h,'direction':0},ui_scale_percent=scale)
        try:
            a.attachment_action('session_info')
            t.wait_for(lambda:a.diag()['view']=='session_info' and not a.diag()['session_info_busy'])
            region=next(x for x in ro(a)['regions'] if x['key']=='page:session_info')
            assert len(region['parts'])>20
            b=region['parts'][1];x=b[0]+8;y=(b[1]+b[3])//2
            # A short hold and a moved-then-paused contact remain unselected.
            press(a,x,y);time.sleep(.48);assert not ro(a)['selected'];press(a,x,y,False)
            a.tap(w-3,h//2)
            press(a,x,y);press(a,x,y+10);time.sleep(.66);assert not ro(a)['selected'];press(a,x,y+10,False)
            a.tap(w-3,h//2)
            # A fresh deliberate contact follows the cancelled scroll after it settles.
            t.wait_for(lambda:not (a.diag().get('page') or {}).get('scrolling',False))
            time.sleep(.2)
            region=next(x for x in ro(a)['regions'] if x['key']=='page:session_info')
            visible=[b for b in region['parts'] if b[1]>=40 and b[3]<h]
            b=visible[0];x=b[0]+8;y=(b[1]+b[3])//2
            press(a,x,y);time.sleep(.66);assert ro(a)['selected'],ro(a);before=ro(a)['end']
            target=visible[min(4,len(visible)-1)]
            press(a,target[0]+30,(target[1]+target[3])//2);time.sleep(.08)
            press(a,target[0]+30,(target[1]+target[3])//2,False)
            assert ro(a)['end']>before and len(ro(a)['handles'])==2
            initial_scroll=a.diag()['scroll_y']
            grip=ro(a)['handle_grips'][-1]
            press(a,*grip);press(a,grip[0],h-3);time.sleep(.25)
            assert a.diag()['scroll_y']>initial_scroll
            press(a,grip[0],h-3,False)
            time.sleep(.1);capture(a,OUT/f'group-{w}-{mode}-{scale}.png')
            menu=ro(a)['menu_bounds'];a.tap(menu[0]+(menu[2]-menu[0])//4,(menu[1]+menu[3])//2)
            assert ro(a)['end']==region['length']
            copy_selection(a);assert ro(a)['clipboard_bytes']>region['length']
            a.to_chat();tap(a,a.diag()['composer_text_bounds'])
            t.wait_for(lambda:a.diag()['editor']['active'])
            if 'clipboard' not in a.diag()['editor']['actions']:a.editor_action('tools')
            a.editor_action('clipboard');a.editor_action('clip-paste:0')
            a.editor_action('official');q=a.receive('keyboard.open')
            text=q['text']
            assert text.index('会话信息')<text.index('当前上下文')<text.index('最大长度')<text.index('最近一次请求')
            assert '当前上下文 · 估算 token' in text and '输出 token · 含推理' in text
            a.send('keyboard.result',requestId=q['requestId'],confirmed=False,text='')
        finally:a.close()
    print('Grouped reading',w,h,'passed',flush=True)


def source_and_summary_check():
    for view in ('summary','web'):
        with tempfile.TemporaryDirectory(prefix='penchat-reading-page-') as folder:
            p=pathlib.Path(folder);fixture(p)
            source=('WebAlpha paragraph one.\n\nSecond paragraph with words.\n'*500)
            with sqlite3.connect(p/'penchat.db') as db:
                if view=='summary':
                    db.execute("UPDATE chat_sessions SET metadata_json=? WHERE id='s'",(json.dumps({'context_compression':{'version':1,'enabled':False,'through':2,'summary':'First paragraph line one.\nSecond line of this paragraph.\n\n第二段说明文字，继续用于跨行选区检查。'}}),))
                else:
                    db.execute("UPDATE chat_messages SET output_text=?,metadata_json=? WHERE id='a'",('[Open cached source](https://example.invalid/source)',json.dumps({'web_sources':[{'title':'Cached source','url':'https://example.invalid/source','text':source,'links':[]}]})))
            a=t.Native(BINARY,p,official_adapter=False)
            try:
                if view=='summary':
                    a.attachment_action('summary');t.wait_for(lambda:a.diag()['view']=='context_summary')
                    region=next(x for x in ro(a)['regions'] if x['key']=='page:context_summary')
                    b=region['parts'][-1]
                else:
                    link=next(x for x in ro(a)['regions'] if x['key']=='a')['links'][0]
                    tap(a,link);t.wait_for(lambda:a.diag()['view']=='web_source')
                    region=next(x for x in ro(a)['regions'] if x['key'].startswith('source:'))
                    assert region['length']==len(source);b=region['parts'][0]
                x=b[0]+12;y=max(8,b[1]+10)
                press(a,x,y);time.sleep(.66);assert ro(a)['selected']
                press(a,x+130,min(240,y+35));press(a,x+130,min(240,y+35),False)
                time.sleep(.15)
                capture(a,OUT/f'{view}-before-all.png')
                menu=ro(a)['menu_bounds'];a.tap(menu[0]+(menu[2]-menu[0])//4,(menu[1]+menu[3])//2)
                assert ro(a)['menu'],(view,ro(a))
                copy_selection(a)
                assert ro(a)['clipboard_bytes']>100
                if view=='web':assert ro(a)['clipboard_bytes']==len(source.encode())
                capture(a,OUT/f'{view}-selection.png')
            finally:a.close()
    print('Summary and cached paged web selection passed',flush=True)

def draft_and_edit_check():
    from test_photo_editor import begin
    from image_gestures import action
    from test_reasoning_ui import seed
    from test_message_edit import fixture as edit_fixture,begin as begin_edit,rows
    with tempfile.TemporaryDirectory(prefix='penchat-draft-image-popup-') as folder:
        p=pathlib.Path(folder);seed(p,1);a=t.Native(BINARY,p)
        try:
            a.edit('保留草稿');begin(a,p,w=60,h=80);action(a,'confirm')
            t.wait_for(lambda:a.diag()['view']=='chat' and not a.diag()['modal'])
            photo=a.diag()['chat_images'][0]
            a.long_press(photo['x']+photo['width']//2,photo['y']+photo['height']//2,.66)
            tap(a,a.diag()['context_menu']['choices']['delete'])
            t.wait_for(lambda:not a.diag()['chat_images'])
            assert a.diag()['draft_length']==4
        finally:a.close()
        a=t.Native(BINARY,p)
        try:assert a.diag()['draft_length']==4 and not a.diag()['chat_images']
        finally:a.close()
    with tempfile.TemporaryDirectory(prefix='penchat-edit-image-popup-') as folder:
        p=pathlib.Path(folder);edit_fixture(p,1);original=rows(p);a=t.Native(BINARY,p)
        try:
            request=begin_edit(a,False)
            a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=request['text'])
            t.wait_for(lambda:not a.diag()['keyboard'] and a.diag()['message_interactions']['editing'])
            photo=next(x for x in a.diag()['chat_images'] if 0<x['y']+x['height']//2<200)
            a.long_press(photo['x']+photo['width']//2,photo['y']+photo['height']//2,.66)
            before=a.diag()['message_interactions']['images']
            tap(a,a.diag()['context_menu']['choices']['delete'])
            t.wait_for(lambda:a.diag()['message_interactions']['images']==before-1)
            a.tap(40,30);assert not a.diag()['message_interactions']['editing']
            assert rows(p)==original and (p/'original.png').is_file()
        finally:a.close()
    print('Durable draft and message-edit image deletion passed',flush=True)

def empty_check():
    from test_reasoning_ui import seed
    for configured,w,h in ((True,960,266),(False,960,266),(True,568,1210)):
        with tempfile.TemporaryDirectory(prefix='penchat-empty-chat-') as folder:
            p=pathlib.Path(folder)
            if configured:seed(p,1)
            a=t.Native(BINARY,p,seed_model=False,screen={'width':w,'height':h,'direction':0})
            try:capture(a,OUT/f'empty-{w}-{configured}.png')
            finally:a.close()
    print('New-chat title-only and unconfigured short hint captured',flush=True)

if __name__=='__main__':
    for w,h in ((960,266),(640,260),(568,1210)):
        for mode in ('dark','light'):
            for scale in (100,150):
                image_check(w,h,mode,scale)
        reading_check(w,h)
        reading_check(w,h,'light',150)

    source_and_summary_check()
    draft_and_edit_check()
    empty_check()
