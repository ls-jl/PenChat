#!/usr/bin/env python3
import os,pathlib,sqlite3,struct,tempfile,time,zlib
from test_lvgl_app import Native,ROOT,wait_for
from test_chat_style import seed,snapshot
import test_lvgl_app as transport
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
with tempfile.TemporaryDirectory(prefix='penchat-delete-image-') as directory:
    p=pathlib.Path(directory);seed(directory,'')
    def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
    original=p/'original.png'
    original.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',100,100,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([80,150,210])*100)*100))+chunk(b'IEND',b''))
    (p/'assets').mkdir();copies=[]
    with sqlite3.connect(p/'penchat.db') as db:
        db.execute('delete from chat_messages')
        for i in range(2):
            mid='photo'+str(i);copy=p/'assets'/(mid+'.png');copy.write_bytes(original.read_bytes());copies.append(copy)
            db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values(?,'fixture',?,'user','complete','image','',1,1)",(mid,i+1))
            db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,created_at) values(?,'fixture',?,'image','album',?,?,1)",(mid,mid,str(original),str(copy)))
            db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values(?,'fixture',?,0,'image',?)",(mid,mid,mid))
    app=Native(binary,p)
    try:
        time.sleep(.3);app.edit('保留文字草稿')
        photos=sorted(app.diag()['chat_images'],key=lambda a:a['x'])
        def hold(photo):app.long_press(photo['x']+photo['width']//2,photo['y']+photo['height']//2)
        photo=photos[0];x=photo['x']+photo['width']//2;y=photo['y']+photo['height']//2
        app.send('touch',x=x,y=y,pressed=True);time.sleep(.1)
        app.send('touch',x=x+20,y=y,pressed=True);time.sleep(.1)
        app.send('touch',x=x,y=y,pressed=True);time.sleep(.65)
        app.send('touch',x=x,y=y,pressed=False);time.sleep(.1)
        assert not app.diag()['modal']
        hold(photos[0]);assert app.diag()['modal'];snapshot(app,'delete-image-confirm')
        app.tap(890,225);assert not app.diag()['modal'] and len(app.read_messages())==2
        hold(photos[0]);app.tap(770,225);wait_for(lambda:len(app.read_messages())==1)
        wait_for(lambda:not copies[0].exists());assert copies[1].exists() and original.exists()
        hold(app.diag()['chat_images'][0]);app.tap(770,225);wait_for(lambda:len(app.read_messages())==0)
        wait_for(lambda:not copies[1].exists());assert app.diag()['summary']['sessions']==0 and original.exists()
        app.tap(500,232);request=app.receive('keyboard.open');assert request['text']=='保留文字草稿'
        app.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='')
        wait_for(lambda:not app.diag()['keyboard'])
    finally:app.close()
print('Pending images: long press, cancel/confirm, single-photo deletion, original preservation, empty-history cleanup and draft retention passed.')
