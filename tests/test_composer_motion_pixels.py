#!/usr/bin/env python3
"""Inspect real dark/light LVGL frames around a moving floating capsule."""
import json,os,pathlib,sqlite3,struct,tempfile,time,zlib
import test_lvgl_app as ui
from test_chat_style import seed
from image_gestures import capture
ui.port=1
os.environ['PENCHAT_TEST_Y07_ROUNDED']='1'
OUT=ui.ROOT/'.work/composer-theme-motion';OUT.mkdir(parents=True,exist_ok=True)
def pixels(path):
 data=path.read_bytes();at=8;compressed=b''
 while at<len(data):
  size=struct.unpack('>I',data[at:at+4])[0];kind=data[at+4:at+8];chunk=data[at+8:at+8+size];at+=size+12
  if kind==b'IHDR':w,h,depth,color,_,_,interlace=struct.unpack('>IIBBBBB',chunk)
  if kind==b'IDAT':compressed+=chunk
 assert depth==8 and color==6 and interlace==0
 source=zlib.decompress(compressed);stride=w*4;previous=bytearray(stride);result=bytearray()
 for y in range(h):
  offset=y*(stride+1);kind=source[offset];row=bytearray(source[offset+1:offset+stride+1])
  for x in range(stride):
   a=row[x-4] if x>=4 else 0;b=previous[x];c=previous[x-4] if x>=4 else 0
   if kind==0:predict=0
   elif kind==1:predict=a
   elif kind==2:predict=b
   elif kind==3:predict=(a+b)//2
   else:
    assert kind==4;p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c);predict=a if pa<=pb and pa<=pc else b if pb<=pc else c
   row[x]=(row[x]+predict)&255
  result.extend(row);previous=row
 return w,h,result
for theme in ['dark','light']:
 with tempfile.TemporaryDirectory(prefix='composer-motion-pixels-') as folder:
  seed(folder,'```text\n'+'\n'.join('background line %03d'%i for i in range(160))+'\n```')
  with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:db.execute('insert into app_settings values(?,?,1)',('theme_mode',json.dumps(theme)))
  app=ui.Native(ui.ROOT/'build/host/penchat-lvgl',pathlib.Path(folder),official_adapter=False,screen={'width':568,'height':1210})
  records=[]
  try:
   ui.wait_for(lambda:not app.diag()['chat_render_pending'])
   for y in range(200,501,30):app.send('touch',x=280,y=y,pressed=True);time.sleep(.025)
   app.send('touch',x=280,y=500,pressed=False);time.sleep(.5)
   assert not app.diag()['following_latest']
   def frame(name):
    app.diag() # Service bridge/OCR acknowledgements while recording frames.
    path=app.directory/'lvgl-diagnostic.json'
    old=json.loads(path.read_text())['diagnostic_time'] if path.exists() else -1
    image=OUT/(theme+'-'+name+'.png');capture(app,image)
    def state():
     s=json.loads((app.directory/'lvgl-diagnostic.json').read_text());return s if s['diagnostic_time']>old else None
    s=ui.wait_for(state);records.append((image,s));return s
   frame('before')
   b=app.diag()['composer_text_bounds'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
   app.send('touch',x=x,y=y,pressed=True);time.sleep(.025);app.send('touch',x=x,y=y,pressed=False)
   for i in range(8):frame('open-'+str(i));time.sleep(.01)
   ui.wait_for(lambda:app.diag()['editor']['keys'] and not app.diag()['surface_motion'])
   b=app.diag()['editor']['actions']['done'];x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
   app.send('touch',x=x,y=y,pressed=True);time.sleep(.025);app.send('touch',x=x,y=y,pressed=False)
   for i in range(8):frame('close-'+str(i));time.sleep(.01)
   assert not app.diag()['editor']['active']
  finally:app.close()
  bg=(0,0,0) if theme=='dark' else (255,255,255)
  code=(24,24,24) if theme=='dark' else (240,242,246)
  checked=0
  for image,state in records:
   w,h,data=pixels(image);x1,y1,x2,y2=state['composer_bounds']
   assert x1==20 and x2==547,(theme,state['composer_bounds'])
   def rgb(x,y):return tuple(data[(y*w+x)*4:(y*w+x)*4+3])
   # Rounded corners must show the code backdrop, not a flat page-colored strip.
   for x,y in [(x2-5,y1+5)]:
    if 0<=y<h:assert rgb(x,y)==code,(image.name,x,y,rgb(x,y),code)
   covered=state['editor']['active'] or state['surface_motion']
   # Lower corners are covered only while editing or finishing keyboard motion.
   for x,y in [(x1+5,y2-5),(x2-5,y2-5)]:
    if 0<=y<h:
     if covered:assert rgb(x,y)==bg,(image.name,'lower corner',x,y,rgb(x,y))
     else:assert rgb(x,y)!=bg,(image.name,'closed corner must reveal content')
   if y2+3<h:
    expected=bg if covered else code
    assert rgb(x2-5,y2+3)==expected,(image.name,'below capsule',rgb(x2-5,y2+3),expected)
   checked+=1
  (OUT/(theme+'-frames.json')).write_text(json.dumps([{'image':str(p),'composer':s['composer_bounds'],'moving':s['surface_motion']} for p,s in records],indent=2))
  for stage in ['open','close']:
   assert any(s['surface_motion'] for p,s in records if '-'+stage+'-' in p.name),(theme,stage,'No animation frames captured')
  print(theme,'transparent corner / no below-capsule text frames:',checked,flush=True)
