#!/usr/bin/env python3
"""Light input recoloring keeps identical editor/key geometry and existing structure."""
import json,os,pathlib,sqlite3,tempfile,time
from PIL import Image
import test_lvgl_app as t
from image_gestures import capture

t.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
out=t.ROOT/'.work/input-stability/keyboard-color-only';out.mkdir(parents=True,exist_ok=True)
for w,h,split in [(960,266,False),(640,260,False),(640,260,True)]:
 for scale in [75,100,125,150]:
  for text_scale in ([100,150] if scale==150 else [100]):
   geometry=None
   for theme in ['dark','light']:
    with tempfile.TemporaryDirectory(prefix='penchat-input-color-') as folder:
     schema=(t.ROOT/'native/lvgl-app/src/schema.hpp').read_text().split('R"SQL(')[1].split(')SQL"')[0]
     with sqlite3.connect(pathlib.Path(folder)/'penchat.db') as db:
      db.executescript(schema)
      for k,v in [('theme_mode',theme),('reading_font_percent',text_scale)]:db.execute('insert into app_settings values(?,?,1)',(k,json.dumps(v)))
     a=t.Native(binary,folder,seed_model=False,official_adapter=False,screen={'width':w,'height':h},ui_scale_percent=scale)
     try:
      b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
      t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['keys'])
      if split:a.editor_action('compact-view')
      time.sleep(.3)
      d=a.diag()['editor'];b=d['text_bounds'];space=d['coordinate_space']
      measured={k:d[k] for k in ['keys','text_bounds','actions','candidate_y','function_y','coordinate_space','keyboard_split']}
      if geometry is None:geometry=measured
      else:assert measured==geometry,(w,split,scale,text_scale,'theme changed geometry')
      path=out/f'{w}-{split}-{scale}-{text_scale}-{theme}.png';capture(a,path)
      if theme=='light':
       def point(x,y):return (round(x*w/space['width']),round(y*h/space['height']))
       im=Image.open(path).convert('RGB');left,top=point(b[0],b[1]);right,bottom=point(b[2],b[3])
       white=im.getpixel((max(left+5,right-10),max(top+3,bottom-5)))
       assert min(white)>240,(path,white,b,space)
     finally:a.close()
   print('LIGHT_INPUT_COLOR_ONLY_GEOMETRY_UNCHANGED',w,split,scale,text_scale,flush=True)
