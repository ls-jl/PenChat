#!/usr/bin/env python3
"""Real LVGL crop interaction and full-resolution image persistence."""
import json,math,os,pathlib,sqlite3,struct,tempfile,time,zlib
from test_lvgl_app import Native,ROOT,wait_for
from test_chat_style import snapshot
import test_lvgl_app as transport
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
with tempfile.TemporaryDirectory(prefix='penchat-crop-') as directory:
    p=pathlib.Path(directory);album=p/'system-album';album.mkdir()
    os.environ['PENCHAT_TEST_ALBUM']=str(album)
    def make_photo():
        folder=p/'camera-tmp';folder.mkdir(exist_ok=True)
        path=folder/'photo.png'
        def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
        pixels=b''.join(b'\0'+b''.join(bytes([x%256,y%256,80]) for x in range(800)) for y in range(600))
        path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',800,600,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b''))
        return path
    def begin(app):
        path=make_photo();app.send('camera.test',photo={'path':str(path),'width':800,'height':600,'source':'camera'})
        wait_for(lambda:app.diag()['view']=='crop');time.sleep(.2)
        return path
    app=Native(binary,p)
    try:
        app.edit('照片说明')
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("insert or replace into app_settings(key,value_json,updated_at) values('penchat_settings',?,1)",(json.dumps({'autoSendCapturedContent':True}),))
        begin(app);state=app.diag();assert not state['crop_save_album']
        w,h=state['crop_preview'];left=86+12+(630-w)//2;top=(266-h)//2
        app.send('touch',x=left+3,y=top+3,pressed=True);time.sleep(.08)
        for step in range(1,13):
            app.send('touch',x=left+3+50*step//12,y=top+3+40*step//12,pressed=True);time.sleep(.025)
        app.send('touch',x=left+53,y=top+43,pressed=False);time.sleep(.1)
        rect=app.diag()['crop_rect'];assert rect[0]>=45 and rect[1]>=35,rect
        snapshot(app,'camera-crop')
        app.tap(835,180);wait_for(lambda:app.diag()['view']=='chat')
        assert not app.diag()['generating'] and not list(album.iterdir())
        with sqlite3.connect(p/'penchat.db') as db:
            row=db.execute('select width,height,stored_path,original_path from message_assets').fetchone()
        assert row[:2]==(800-math.floor(rect[0]/w*800),600-math.floor(rect[1]/h*600)),row
        assert pathlib.Path(row[2]).exists() and row[2]==row[3]
        assert not list((p/'camera-tmp').iterdir())
        app.tap(500,232);request=app.receive('keyboard.open');assert request['text']=='照片说明'
        app.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='');wait_for(lambda:not app.diag()['keyboard'])
        begin(app);app.tap(830,120);assert app.diag()['crop_save_album']
        app.tap(835,180);wait_for(lambda:app.diag()['view']=='chat')
        assert len(list(album.glob('*.jpg')))==1
        begin(app);assert app.diag()['crop_save_album'];app.tap(40,32);wait_for(lambda:app.diag()['view']=='chat')
        assert not list((p/'camera-tmp').iterdir()) and len(list(album.iterdir()))==1
    finally:app.close()
    app=Native(binary,p)
    try:
        begin(app);assert app.diag()['crop_save_album']
        app.tap(830,120);app.tap(835,180);wait_for(lambda:app.diag()['view']=='chat')
        assert len(list(album.iterdir()))==1
        begin(app)
    finally:app.close()
    assert not list((p/'camera-tmp').iterdir())
print('Camera crop: corner mapping, original-resolution crop, pending-only confirmation, draft, album opt-in persistence, cancel and exit cleanup passed.')
