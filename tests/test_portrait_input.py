#!/usr/bin/env python3
"""Rounded-corner safe areas and original-widget native input. Host only, no devices."""
import http.server,json,os,pathlib,sqlite3,struct,tempfile,threading,time,zlib
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/portrait-input';OUT.mkdir(parents=True,exist_ok=True)
def root_chat(a):
    for _ in range(12):
        d=a.diag()
        if d['view']=='chat':
            if d['navigation_open']:a.tap(548,100)
            return
        for i in range(9):a.send('touch',x=12+18*i,y=400,pressed=True);time.sleep(.025)
        a.send('touch',x=156,y=400,pressed=False);time.sleep(.15)
    raise AssertionError('Cannot return using native back gesture')
def open_settings(a):
    root_chat(a);a.open_settings()
def focus(a,anchor):
    targets=a.diag()['editor']['targets']
    identity=max((k for k in targets if k==anchor or k.startswith(anchor+':')),key=lambda k:targets[k][1])
    x,y,r,b=targets[identity]
    a.tap((x+r)//2,(y+b)//2)
    t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['input_anchor']==identity)
def key(a,value):
    k=next(k for k in a.diag()['editor']['keys'] if k['key']==value)
    a.tap(k['x'],k['y'])
def swipe(a,dy):
    start=450 if dy<0 else 200
    for i in range(9):a.send('touch',x=280,y=start+dy*i//8,pressed=True);time.sleep(.025)
    a.send('touch',x=280,y=start+dy,pressed=False);time.sleep(.2)
def shot(a,name):
    path=OUT/(name+'.png');capture(a,path)
    if name in ('provider-bottom-input','secret-bottom-input'):
        # Read only the header rows of the native RGBA screenshot, no image dependency.
        data=path.read_bytes();w,h,depth,kind=struct.unpack('>IIBB',data[16:26]);assert (depth,kind)==(8,6)
        chunks=[];at=8
        while at<len(data):
            n=struct.unpack('>I',data[at:at+4])[0]
            if data[at+4:at+8]==b'IDAT':chunks.append(data[at+8:at+8+n])
            at+=n+12
        raw=zlib.decompress(b''.join(chunks));previous=bytearray(w*4);white=0
        for y in range(61):
            at=y*(w*4+1);kind=raw[at];row=bytearray(raw[at+1:at+1+w*4])
            for x in range(len(row)):
                left=row[x-4] if x>=4 else 0;up=previous[x];diagonal=previous[x-4] if x>=4 else 0
                if kind==1:row[x]=(row[x]+left)&255
                elif kind==2:row[x]=(row[x]+up)&255
                elif kind==3:row[x]=(row[x]+(left+up)//2)&255
                elif kind==4:
                    p=left+up-diagonal;da,db,dc=abs(p-left),abs(p-up),abs(p-diagonal)
                    row[x]=(row[x]+(left if da<=db and da<=dc else up if db<=dc else diagonal))&255
                else:assert kind==0
            if y>=30:white+=sum(min(row[x:x+3])>140 for x in range(80*4,385*4,4))
            previous=row
        assert white>100, 'Original form heading disappeared under input panel'

def run():
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),t.Server)
    t.port=server.server_port;threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix='penchat-portrait-input-') as directory:
            p=pathlib.Path(directory);seed(p,t.port)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("insert into chat_sessions(id,title,status,created_at,updated_at,last_message_at) values('s','竖屏输入测试','active',1,1,1)")
                db.execute("insert into app_settings values('active_session_id','\"s\"',1)")
                db.execute("insert into app_settings values('auto_title_enabled','false',1)")
                db.execute("insert into app_settings values('chat_presets',?,1)",(json.dumps([{'id':'default','name':'默认预设','prompt_mode':'inherit'},{'id':'fixture','name':'多行测试','prompt_mode':'custom','prompt':'First line.\nSecond line.'}]),))
                for i,role,text in [(1,'user','保留聊天内容，让键盘从底部弹出。'),(2,'assistant','\n\n'.join('原页面仍然可以浏览，这是第%d段。'%i for i in range(30)))]:
                    db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'s',?,?,'complete','text',?,1,1)",('m'+str(i),i,role,text))
            a=t.Native(BINARY,p,screen={'width':568,'height':1210,'direction':180,'tp_direction':180},official_adapter=False)
            try:
                state=a.diag();assert state['safe_insets']==[24,24,24,28]
                assert state['toolbar_y']==1088
                shot(a,'chat-safe')
                a.tap(280,1158);t.wait_for(lambda:a.diag()['editor']['active'])
                state=a.diag();e=state['editor'];assert e['docked'] and e['inline_chat'] and e['panel_top']==623
                assert e['in_place'] and e['text_in_target'] and e['input_anchor']=='chat-composer'
                assert e['target_bounds'][0]<=e['text_bounds'][0]<e['text_bounds'][2]<=e['target_bounds'][2]
                assert e['target_bounds'][1]<=e['text_bounds'][1]<e['text_bounds'][3]<=e['target_bounds'][3]
                for k in e['keys']:
                    x,y,r,b=k['bounds'];assert x>=0 and r<=568 and b<=1172 and r-x>=44 and b-y>=(44 if k['key']=='language' else 84),k
                assert min(k['bounds'][1] for k in e['keys'])==705
                assert max(k['bounds'][3] for k in e['keys'] if k['key']!='language')==1122
                assert 'cancel' not in e['actions']
                assert e['actions']['done'][1]>1122
                assert next(k for k in e['keys'] if k['key']=='language')['y']==1145
                assert next(k for k in e['keys'] if k['key']=='space')['bounds'][2]-next(k for k in e['keys'] if k['key']=='space')['bounds'][0]>210
                shot(a,'chat-keyboard')
                key(a,'language')
                for c in 'test':key(a,c)
                assert a.diag()['editor']['length']==4
                bounds=a.diag()['editor']['text_bounds'];tx,ty=bounds[0]+12,bounds[1]+18
                a.long_press(tx,ty);t.wait_for(lambda:a.diag()['editor']['undo_menu'])
                bubble=a.diag()['editor']['undo_popup'];assert bubble[3]<=ty-60,bubble
                shot(a,'long-press-bubble');a.tap(tx,ty)
                a.send('touch',x=tx,y=ty,pressed=True);time.sleep(.52)
                a.send('touch',x=tx+60,y=ty,pressed=True)
                t.wait_for(lambda:a.diag()['editor']['magnifier'])
                assert a.diag()['editor']['magnifier_bounds'][3]<=bounds[1]-50
                shot(a,'raised-magnifier')
                a.send('touch',x=tx+60,y=ty,pressed=False)
                t.wait_for(lambda:not a.diag()['editor']['cursor_drag'])
                before=a.diag()['scroll_y'];swipe(a,200)
                assert a.diag()['editor']['active'] and a.diag()['scroll_y']!=before
                assert a.diag()['editor']['target_bounds'][3]<a.diag()['editor']['panel_top']
                shot(a,'chat-reading-with-keyboard')
                a.editor_action('official');request=a.receive('keyboard.open')
                assert request['text']=='test'
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard'])
                assert a.diag()['editor']['length']==4 and a.diag()['editor']['docked']
                a.editor_action('done');t.wait_for(lambda:not a.diag()['editor']['active'])
                assert len(a.read_messages())==2 # Done never sends.
                d=a.diag()
                if d['to_bottom_visible']:a.tap(d['to_bottom_x']+22,d['to_bottom_y']+22)
                t.wait_for(lambda:a.diag()['toolbar_y']==1088)
                a.tap(280,1158);t.wait_for(lambda:a.diag()['editor']['active'])
                assert a.diag()['editor']['length']==4
                a.editor_action('chat-send');t.wait_for(lambda:not a.diag()['editor']['active'])
                t.wait_for(lambda:len(a.read_messages())==4 and not a.diag()['generating'])
                assert a.read_messages()[-2][2]=='test'
                open_settings(a);shot(a,'settings-safe');a.setting('models');a.page_action('添加 Provider')
                shot(a,'provider-safe');a.tap(220,108)
                t.wait_for(lambda:a.diag()['editor']['active'])
                assert a.diag()['editor']['docked'] and not a.diag()['editor']['inline_chat']
                assert a.diag()['editor']['text_in_target'] and a.diag()['editor']['text_bounds'][1]<200
                shot(a,'provider-bottom-input')
                # A different original field commits the first one without losing
                # unselected pinyin, then focuses that exact widget in one tap.
                for c in 'nihao':key(a,c)
                assert a.diag()['editor']['preedit_length']==5
                first_session=a.diag()['editor']['session']
                a.tap(220,165)
                t.wait_for(lambda:a.diag()['editor']['input_anchor'].startswith('field:Base URL:'))
                assert a.diag()['editor']['session']!=first_session
                assert a.diag()['editor']['text_in_target']
                a.tap(220,110)
                t.wait_for(lambda:a.diag()['editor']['input_anchor'].startswith('field:名称:'))
                a.editor_action('official');request=a.receive('keyboard.open')
                assert request['text']=='OpenAInihao',request
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard'])
                a.editor_action('cancel');t.wait_for(lambda:not a.diag()['editor']['active'])
                a.tap(220,240);t.wait_for(lambda:a.diag()['editor']['active'])
                assert a.diag()['editor']['secret'] and a.diag()['editor']['docked']
                key(a,'a');assert a.diag()['editor']['length']==1
                shot(a,'secret-bottom-input')
                a.editor_action('official');request=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text='test-only-key')
                t.wait_for(lambda:not a.diag()['editor']['active'])
                assert a.diag()['view']=='provider'
                # No Provider save was requested.
                with sqlite3.connect(p/'penchat.db') as db:assert db.execute('select count(*) from providers').fetchone()[0]==1
                open_settings(a);a.setting('http_idle');focus(a,'setting:http_idle_seconds')
                assert a.diag()['editor']['text_in_target']
                a.editor_action('official');request=a.receive('keyboard.open');assert request['text']=='90'
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard']);a.editor_action('cancel')
                # A long field expands the actual form row, not a duplicate box.
                open_settings(a);a.setting('presets');a.tap(260,288)
                focus(a,'field:系统提示词');e=a.diag()['editor']
                assert e['text_in_target'] and e['target_bounds'][3]-e['target_bounds'][1]==143
                assert e['text_bounds'][3]<e['panel_top']
                shot(a,'prompt-in-place');a.editor_action('cancel')
                # Rename stays in the original history title reader, even though
                # its drawer closes behind the rename context.
                root_chat(a);a.open_navigation();row=a.diag()['history_rows'][0]
                a.long_press(150,row['y'])
                t.wait_for(lambda:a.diag()['history_context'])
                a.history_action('rename');t.wait_for(lambda:a.diag()['editor']['active'])
                assert a.diag()['editor']['input_anchor'].startswith('history-title:')
                assert a.diag()['editor']['text_in_target'];shot(a,'history-rename-in-place')
                a.editor_action('cancel');t.wait_for(lambda:not a.diag()['editor']['active'])
                a.to_chat();a.tap(280,1158);t.wait_for(lambda:a.diag()['editor']['active'])
                for c in 'nihao':key(a,c)
                t.wait_for(lambda:a.diag()['editor']['candidate_count']>0)
                e=a.diag()['editor'];assert e['shared_candidate_row'] and e['candidate_y']==e['function_y']
                assert 'official' not in e['actions'] and 'ocr-mode' not in e['actions'] and 'clipboard' not in e['actions']
                assert e['panel_top']==623
                shot(a,'shared-candidates')
                a.editor_action('expand');shot(a,'expanded-candidates')
                a.editor_action('expand');key(a,'space');t.wait_for(lambda:a.diag()['editor']['length']>0)
                k=next(k for k in a.diag()['editor']['keys'] if k['key']=='language')
                a.long_press(k['x'],k['y']);a.editor_action('layout:2');shot(a,'nine-key')
                a.editor_action('clipboard');shot(a,'clipboard');a.editor_action('clip-close')
                a.send('visibility',visible=False);a.send('visibility',visible=True)
                t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['docked'])
                if a.diag()['editor']['preedit_length']:key(a,'enter')
                a.editor_action('done')
            except BaseException:
                state=a.diag();(OUT/'failure.json').write_text(json.dumps(state,ensure_ascii=False,indent=2));shot(a,'failure');raise
            finally:a.close()
        # The assistant may finish/rebuild while a new draft is being edited.
        with tempfile.TemporaryDirectory(prefix='penchat-input-stream-') as directory:
            p=pathlib.Path(directory);seed(p,t.port)
            with sqlite3.connect(p/'penchat.db') as db:
                db.execute("update model_configs set name='scroll' where id='a'")
                db.execute("insert into app_settings values('auto_title_enabled','false',1)")
            a=t.Native(BINARY,p,screen={'width':568,'height':1210},official_adapter=True)
            try:
                a.edit('Question');d=a.diag();a.tap(d['send_center_x2']//2,d['send_center_y2']//2);t.wait_for(lambda:a.diag()['generating'])
                a.official_adapter=False
                a.tap(280,1158);t.wait_for(lambda:a.diag()['editor']['active'])
                key(a,'language');key(a,'q');session=a.diag()['editor']['session']
                t.wait_for(lambda:not a.diag()['generating'],timeout=12)
                e=a.diag()['editor'];assert e['active'] and e['session']==session and e['text_in_target'] and e['length']==1,e
                shot(a,'reply-finished-draft-preserved')
                a.editor_action('official');request=a.receive('keyboard.open');assert request['text']=='q'
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard']);a.editor_action('done')
                assert len(a.read_messages())==2
            finally:a.close()
        # The real composer grows upward, caps at 3x, and then scrolls.
        with tempfile.TemporaryDirectory(prefix='penchat-input-grow-') as directory:
            a=t.Native(BINARY,pathlib.Path(directory),screen={'width':568,'height':1210},official_adapter=False)
            try:
                a.tap(280,1155);t.wait_for(lambda:a.diag()['editor']['active'])
                key(a,'language');key(a,'a')
                initial=a.diag()['composer_bounds'];bottom=initial[3]
                assert initial[3]-initial[1]+1==94
                shot(a,'composer-one-line')
                heights=[]
                for i in range(10):
                    key(a,'enter');key(a,'a');d=a.diag();b=d['composer_bounds'];heights.append(b[3]-b[1]+1)
                    assert b[3]==bottom and 94<heights[-1]<=282
                    arrow=d['editor']['actions']['chat-send']
                    assert b[2]-arrow[2]==25 and b[3]-arrow[3]==25,(b,arrow)
                    if i==1:shot(a,'composer-three-lines')
                assert heights==sorted(heights) and heights[-1]==282
                shot(a,'composer-max-scroll')
                assert a.diag()['editor']['text_scroll']>0,a.diag()['editor']
                for _ in range(18):key(a,'delete')
                b=a.diag()['composer_bounds'];assert 94<b[3]-b[1]+1<282 and b[3]==bottom
                for _ in range(42):key(a,'a')
                b=a.diag()['composer_bounds'];assert 118<b[3]-b[1]+1<=282
                shot(a,'composer-wrapped')
                a.editor_action('done');assert not a.diag()['editor']['active']
                assert a.diag()['draft_length']>0 and not a.read_messages()
            finally:a.close()
        # Same launcher geometry in landscape keeps the existing editor.
        with tempfile.TemporaryDirectory(prefix='penchat-landscape-input-') as directory:
            a=t.Native(BINARY,pathlib.Path(directory),official_adapter=False)
            try:
                assert a.diag()['toolbar_y']==208
                a.tap(350,232);t.wait_for(lambda:a.diag()['editor']['active'])
                assert not a.diag()['editor']['docked']
                assert a.diag()['editor']['text_bounds'][1]==78
                shot(a,'landscape-unchanged');a.editor_action('cancel')
            finally:a.close()
    finally:server.shutdown();server.server_close()
    print('Portrait original-widget input: chat/forms, field switching/raw preedit, prompt expansion, history rename, streaming completion, cancel/official recovery, send, scroll, candidates, nine-key, clipboard and unchanged landscape passed.')
if __name__=='__main__':run()
