#!/usr/bin/env python3
"""Boundary blank lines are absent from both saved prompts and actual requests."""
import http.server,json,os,pathlib,sqlite3,tempfile,threading,time
import test_lvgl_app as t
from test_message_edit import png

BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
for api,w,h in [('responses',960,266),('chat',936,280)]:
    with http.server.ThreadingHTTPServer(('127.0.0.1',0),t.Server) as server,tempfile.TemporaryDirectory(prefix='penchat-blank-chat-') as tmp:
        t.port=server.server_port;t.Server.requests=[]
        threading.Thread(target=server.serve_forever,daemon=True).start()
        p=pathlib.Path(tmp);a=t.Native(BINARY,p,api=api,screen={'width':w,'height':h,'direction':0})
        try:
            for blank in ['\n\n',' \t\r\n','　\n\u00a0','\v\f','\u2028\u2029']:
                a.edit(blank);a.tap(w-52,h-34)
                assert not a.diag()['generating'] and not a.read_messages() and a.diag()['summary']['sessions']==0
            body='    中文 👩‍💻  \n\n正文\n\n```\n  code\n\n  next\n```'
            a.edit('\n　\n'+body+'\n\t\n');a.tap(w-52,h-34)
            t.wait_for(lambda:any(x[0]=='assistant' and x[1]=='complete' for x in a.read_messages()))
            t.wait_for(lambda:not a.diag()['generating'] and not a.diag()['storage']['unsaved'])
            assert [x[2] for x in a.read_messages() if x[0]=='user']==[body]
            assert a.diag()['draft_length']==0
            def request_text():
                request=t.Server.requests[-1][1];messages=request['messages' if api=='chat' else 'input']
                result=[]
                for m in messages:
                    if m.get('role')!='user':continue
                    content=m.get('content',[])
                    if isinstance(content,str):result.append(content)
                    else:result.extend(x['text'] for x in content if x.get('type') in ['text','input_text'])
                return result
            assert request_text()==[body]
            session=a.diag()['session']
        finally:a.close()
        # Pending image-only sends must not append a whitespace text block/row.
        (p/'assets').mkdir(exist_ok=True);photo=p/'assets/blank-pending.png';png(photo)
        with sqlite3.connect(p/'penchat.db') as db:
            seq=db.execute('select max(seq)+1 from chat_messages where session_id=?',(session,)).fetchone()[0]
            stamp=int(time.time()*1000)
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('blank-image',?,?,'user','complete','image','',?,?)",(session,seq,stamp,stamp))
            db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,created_at) values('blank-image',?,'blank-image','image','album',?,?,2)",(session,str(photo),str(photo)))
            db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('blank-image',?,'blank-image',0,'image','blank-image')",(session,))
            db.execute("insert or replace into app_settings values('auto_title_enabled','false',2)")
        a=t.Native(BINARY,p,screen={'width':w,'height':h,'direction':0})
        try:
            before=len(t.Server.requests);a.edit('\n　\n');a.tap(w-52,h-34)
            t.wait_for(lambda:len(t.Server.requests)==before+1)
            t.wait_for(lambda:not a.diag()['generating'] and not a.diag()['storage']['unsaved'])
            assert [x[2] for x in a.read_messages() if x[0]=='user']==[body,'']
            assert request_text()==[body,'请根据这张图片回答。'] and a.diag()['draft_length']==0
            with sqlite3.connect(p/'penchat.db') as db:
                assert db.execute("select p.text from message_parts p join chat_messages m on m.id=p.message_id where m.role='user' and p.part_type='text'").fetchall()==[(body,)]
        finally:a.close()
    print(f'Blank chat {api} {w}x{h}: whitespace-only, trimmed boundaries, intact paragraphs/code, request/DB agreement and image-only send passed.',flush=True)
