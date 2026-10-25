#!/usr/bin/env python3
"""Crop confirmation/album/draft lifecycle, independent of preview background."""
import json,os,pathlib,sqlite3,tempfile
from test_lvgl_app import Native,ROOT,wait_for
from test_photo_editor import begin
from image_gestures import action,drag
import test_lvgl_app as transport
transport.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',ROOT/'build/host/penchat-lvgl'))
with tempfile.TemporaryDirectory(prefix='penchat-crop-') as directory:
    p=pathlib.Path(directory);album=p/'system-album';album.mkdir();os.environ['PENCHAT_TEST_ALBUM']=str(album)
    app=Native(binary,p)
    try:
        app.edit('照片说明')
        with sqlite3.connect(p/'penchat.db') as db:
            db.execute("insert or replace into app_settings(key,value_json,updated_at) values('penchat_settings',?,1)",(json.dumps({'autoSendCapturedContent':True}),))
        begin(app,p);s=app.diag();assert not s['crop_save_album']
        x,y,w,h=s['crop_selection'];drag(app,x+2,y+2,45,35,True)
        expected=app.diag()['crop_source_rect'];action(app,'confirm');wait_for(lambda:app.diag()['view']=='chat')
        assert not app.diag()['generating'] and not list(album.iterdir())
        with sqlite3.connect(p/'penchat.db') as db:row=db.execute('select width,height,stored_path,original_path from message_assets').fetchone()
        assert row[:2]==tuple(expected[2:]) and pathlib.Path(row[2]).exists() and row[2]==row[3]
        assert not list((p/'camera-tmp').iterdir())
        app.tap(500,232);request=app.receive('keyboard.open');assert request['text']=='照片说明'
        app.send('keyboard.result',requestId=request['requestId'],confirmed=False,text='');wait_for(lambda:not app.diag()['keyboard'])
        begin(app,p);action(app,'album');assert app.diag()['crop_save_album']
        action(app,'confirm');wait_for(lambda:app.diag()['view']=='chat');assert len(list(album.glob('*.jpg')))==1
        begin(app,p);assert app.diag()['crop_save_album'];action(app,'cancel');wait_for(lambda:app.diag()['view']=='camera')
    finally:app.close()
    assert not list((p/'camera-tmp').iterdir()) and len(list(album.iterdir()))==1
    app=Native(binary,p)
    try:
        begin(app,p);assert app.diag()['crop_save_album'];action(app,'album');action(app,'confirm')
        wait_for(lambda:app.diag()['view']=='chat');assert len(list(album.iterdir()))==1
        begin(app,p,w=2400,h=32);s=app.diag();x,y,w,h=s['crop_selection']
        drag(app,x+2,y+h/2,30,0,True)
        assert app.diag()['crop_source_rect'][0]>0 and app.diag()['view']=='crop'
        action(app,'reset');action(app,'confirm');wait_for(lambda:app.diag()['view']=='chat')
        with sqlite3.connect(p/'penchat.db') as db:assert db.execute('select width,height from message_assets order by created_at desc limit 1').fetchone()==(2400,32)
    finally:app.close()
print('Camera crop: pending-only, full-resolution/long-image output, draft, album opt-in persistence and exit cleanup passed.')
