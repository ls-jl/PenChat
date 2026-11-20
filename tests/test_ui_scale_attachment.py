#!/usr/bin/env python3
"""Pending image ownership survives scale rebuilds without sending or rewriting."""
import hashlib,json,pathlib,sqlite3,tempfile
from PIL import Image
import test_lvgl_app as t
from test_font_size import fixture
from ui_scale_ui import open_settings,back,choose,save

t.port=1
with tempfile.TemporaryDirectory(prefix='penchat-scale-image-') as folder:
    p=pathlib.Path(folder);fixture(p);(p/'assets').mkdir()
    photo=p/'assets/pending.png';Image.new('RGB',(120,80),(80,40,20)).save(photo)
    original=hashlib.sha256(photo.read_bytes()).hexdigest()
    with sqlite3.connect(p/'penchat.db') as db:
        db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at) values('image','fixture',3,'user','complete','image',1,1)")
        db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,stored_path,original_path,width,height,size_bytes,created_at) values('asset','fixture','image','image','test',?,?,120,80,?,1)",(str(photo),str(photo),photo.stat().st_size))
        db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('part','fixture','image',0,'image','asset')")
    a=t.Native(t.ROOT/'build/host/penchat-lvgl',p,seed_model=False)
    try:
        for percent in [75,150,100]:
            open_settings(a);a.setting('ui_scale');choose(a,percent);save(a,percent);back(a)
            t.wait_for(lambda:len(a.diag()['chat_images'])==1)
            assert a.diag()['session']=='fixture' and not a.diag()['generating']
            with sqlite3.connect(p/'penchat.db') as db:
                assert db.execute("select count(*) from chat_messages where session_id='fixture'").fetchone()[0]==3
                assert db.execute('select count(*) from message_assets').fetchone()[0]==1
            assert hashlib.sha256(photo.read_bytes()).hexdigest()==original
    finally:a.close()
print('SCALE_PENDING_ATTACHMENT_PRESERVED_WITHOUT_SEND')
