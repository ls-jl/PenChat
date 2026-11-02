#!/usr/bin/env python3
"""Replaying onboarding preserves the real reader, unsent text and image assets."""
import hashlib,json,os,pathlib,sqlite3,struct,tempfile,time,zlib
import test_lvgl_app as t
from test_chat_style import seed,drag
from test_onboarding_helpers import action

def png():
 def chunk(k,v):return struct.pack('!I',len(v))+k+v+struct.pack('!I',zlib.crc32(k+v))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',100,100,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([80,150,210])*100)*100))+chunk(b'IEND',b'')
t.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
with tempfile.TemporaryDirectory(prefix='penchat-guide-state-') as directory:
 p=pathlib.Path(directory);seed(p,'\n\n'.join('第%d段保留原始阅读内容。'%i for i in range(25)))
 (p/'assets').mkdir();original=p/'assets/pending.png';original.write_bytes(png())
 with sqlite3.connect(p/'penchat.db') as db:
  db.execute("insert into chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,updated_at) values('pending','fixture',2,'user','complete','image','',2,2)")
  db.execute("insert into message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,created_at) values('pending','fixture','pending','image','album',?,?,2)",(str(original),str(original)))
  db.execute("insert into message_parts(id,session_id,message_id,part_index,part_type,asset_id) values('pending','fixture','pending',0,'image','pending')")
 a=t.Native(binary,p)
 try:
  draft='引导前尚未发送的文字';a.edit(draft);time.sleep(.25);drag(a,[80,110,145,190,225]);t.wait_for(lambda:not a.diag()["reader_interacting"]);time.sleep(.1)
  before=a.diag();messages=a.read_messages();digest=hashlib.sha256(original.read_bytes()).hexdigest()
  a.open_settings();a.setting('about');a.tap(156,222);t.wait_for(lambda:a.diag()['view']=='onboarding')
  action(a,'next');action(a,'finish');t.wait_for(lambda:a.diag()['view']=='settings');a.to_chat();time.sleep(.2)
  after=a.diag();assert after['session']==before['session'] and after['draft_length']==len(draft)
  assert a.read_messages()==messages and hashlib.sha256(original.read_bytes()).hexdigest()==digest
  assert len(after['chat_images'])==len(before['chat_images']) and abs(after['scroll_y']-before['scroll_y'])<=2, {'before': {k:before.get(k) for k in ('scroll_y','chat_images','following','toolbar_y')}, 'after': {k:after.get(k) for k in ('scroll_y','chat_images','following','toolbar_y')}}
  a.tap(521,174);t.wait_for(lambda:a.diag()['toolbar_y']==208)
  a.tap(500,232);request=a.receive('keyboard.open');assert request['text']==draft
 finally:a.close()
print('Onboarding replay: same conversation, exact draft, pending image bytes/associations and reader position preserved.')
