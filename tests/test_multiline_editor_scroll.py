#!/usr/bin/env python3
"""Editing inside multiline text never retains a scroll animation to its tail."""
import sys,pathlib,tempfile,time,json,traceback
root=pathlib.Path.cwd();sys.path.insert(0,str(root/'tests'));import test_lvgl_app as t
from image_gestures import capture
t.port=1;out=root/'.work/multiline-editor-scroll';out.mkdir(parents=True,exist_ok=True);results=[]
for w,h in [(936,280),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory() as folder:
  a=t.Native(root/'build/host/penchat-lvgl',folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   def e():return a.diag()['editor']
   def opening():
    b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2);t.wait_for(lambda:e()['active'] and bool(e()['keys']));time.sleep(.3)
   opening();a.editor_action('official');req=a.receive('keyboard.open');text='\n'.join(f'Line {i:02}: multiline middle insertion and scroll check.' for i in range(18));a.send('keyboard.result',requestId=req['requestId'],confirmed=True,text=text);t.wait_for(lambda:not e()['active']);time.sleep(.3);opening()
   # Move the cursor upwards through visible rows by a captured caret drag.
   b=e()['text_bounds'];x=b[0]+15;y=(b[1]+b[3])//2
   a.send('touch',x=x,y=y,pressed=True);time.sleep(.52)
   a.send('touch',x=x,y=b[1]+5,pressed=True);time.sleep(.1);a.send('touch',x=x,y=b[1]+5,pressed=False);time.sleep(.3)
   def key(value):
    z=e();k=next(k for k in z['keys'] if k['key']==value);space=z['coordinate_space'];a.tap(round(k['x']*w/space['width']),round(k['y']*h/space['height']))
   if e()['chinese']:key('language');time.sleep(.2)
   before=e();capture(a,out/f'{w}-middle-before.png');samples=[]
   key('x')
   for _ in range(10):
    z=e();samples.append({k:z[k] for k in ('cursor','caret','text_scroll','length','text_bounds')});time.sleep(.025)
   assert all(z['cursor']==before['cursor']+1 and z['length']==before['length']+1 for z in samples),samples
   assert max(z['text_scroll'] for z in samples)-min(z['text_scroll'] for z in samples)<=1,samples
   assert all(z['text_bounds'][1]<=z['caret'][1]<=z['text_bounds'][3]+15 for z in samples),samples
   capture(a,out/f'{w}-middle-after.png');row=dict(width=w,height=h,before={k:before[k] for k in ('cursor','caret','text_scroll','length','text_bounds')},samples=samples);results.append(row);print(json.dumps(row),flush=True)
  except Exception:
   traceback.print_exc();raise
  finally:a.close()
(out/'editor-results.json').write_text(json.dumps(results,indent=2))
