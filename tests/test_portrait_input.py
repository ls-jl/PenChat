#!/usr/bin/env python3
"""Rounded-corner safe areas and docked native input. Host only, no devices."""
import http.server,json,os,pathlib,sqlite3,struct,tempfile,threading,time,zlib
import test_lvgl_app as t
from test_reasoning_ui import seed
from image_gestures import capture
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/portrait-input';OUT.mkdir(parents=True,exist_ok=True)
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
                for i,role,text in [(1,'user','保留聊天内容，让键盘从底部弹出。'),(2,'assistant','\n\n'.join('原页面仍然可以浏览，这是第%d段。'%i for i in range(30)))]:
                    db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'s',?,?,'complete','text',?,1,1)",('m'+str(i),i,role,text))
            a=t.Native(BINARY,p,screen={'width':568,'height':1210,'direction':180,'tp_direction':180},official_adapter=False)
            try:
                state=a.diag();assert state['safe_insets']==[24,24,24,28]
                assert state['toolbar_y']==1134
                shot(a,'chat-safe')
                a.tap(280,1158);t.wait_for(lambda:a.diag()['editor']['active'])
                state=a.diag();e=state['editor'];assert e['docked'] and e['inline_chat'] and 600<e['panel_top']<800
                assert e['text_bounds'][1]>600
                for k in e['keys']:
                    x,y,r,b=k['bounds'];assert x>=24 and r<=544 and b<=1182 and r-x>=44 and b-y>=44,k
                shot(a,'chat-keyboard')
                key(a,'language')
                for c in 'test':key(a,c)
                assert a.diag()['editor']['length']==4
                before=a.diag()['scroll_y'];swipe(a,200)
                assert a.diag()['editor']['active'] and a.diag()['scroll_y']!=before
                shot(a,'chat-reading-with-keyboard')
                a.editor_action('official');request=a.receive('keyboard.open')
                assert request['text']=='test'
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard'])
                assert a.diag()['editor']['length']==4 and a.diag()['editor']['docked']
                a.editor_action('done');t.wait_for(lambda:not a.diag()['editor']['active'])
                assert len(a.read_messages())==2 # Done never sends.
                a.tap(284,1100);t.wait_for(lambda:a.diag()['toolbar_y']==1134)
                a.tap(280,1158);t.wait_for(lambda:a.diag()['editor']['active'])
                assert a.diag()['editor']['length']==4
                a.editor_action('chat-send');t.wait_for(lambda:not a.diag()['editor']['active'])
                t.wait_for(lambda:len(a.read_messages())==4 and not a.diag()['generating'])
                assert a.read_messages()[-2][2]=='test'
                a.open_settings();shot(a,'settings-safe');a.setting('models');a.page_action('添加 Provider')
                shot(a,'provider-safe');a.tap(220,108)
                t.wait_for(lambda:a.diag()['editor']['active'])
                assert a.diag()['editor']['docked'] and not a.diag()['editor']['inline_chat']
                shot(a,'provider-bottom-input')
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
                a.to_chat();a.tap(280,1158);t.wait_for(lambda:a.diag()['editor']['active'])
                for c in 'nihao':key(a,c)
                t.wait_for(lambda:a.diag()['editor']['candidate_count']>0)
                a.editor_action('expand');shot(a,'expanded-candidates')
                a.editor_action('expand');key(a,'space');t.wait_for(lambda:a.diag()['editor']['length']>0)
                k=next(k for k in a.diag()['editor']['keys'] if k['key']=='language')
                a.long_press(k['x'],k['y']);a.editor_action('layout:2');shot(a,'nine-key')
                a.editor_action('clipboard');shot(a,'clipboard');a.editor_action('clip-close')
                a.send('visibility',visible=False);a.send('visibility',visible=True)
                t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['docked'])
                a.editor_action('cancel')
            except BaseException:
                state=a.diag();(OUT/'failure.json').write_text(json.dumps(state,ensure_ascii=False,indent=2));shot(a,'failure');raise
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
    print('Portrait safe insets/docked chat and forms: real input/send, background scroll, official cancel/confirm, candidates, nine-key, clipboard, lifecycle and landscape preservation passed.')
if __name__=='__main__':run()
