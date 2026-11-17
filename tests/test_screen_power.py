#!/usr/bin/env python3
"""Screen power gates retained LVGL state independently of Falcon focus."""
import json,os,pathlib,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed

t.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
for w,h in [(960,266),(640,260),(568,1210)]:
 with tempfile.TemporaryDirectory(prefix='penchat-screen-power-') as folder:
  seed(folder,'\n\n'.join('保留阅读状态 '+str(i) for i in range(60)))
  a=t.Native(binary,folder,official_adapter=False,screen={'width':w,'height':h})
  try:
   sequence=0;renew=True;lastPower=0;powerAwake=True;powerKnown=True
   originalDiag=a.diag
   def diag():
    global sequence,lastPower
    if renew and sequence and time.monotonic()-lastPower>.4:
     sequence+=1;lastPower=time.monotonic()
     a.send('screen.power',sequence=sequence,available=True,known=powerKnown,awake=powerAwake)
    return originalDiag()
   a.diag=diag
   def power(awake,known=True):
    global sequence,lastPower,powerAwake,powerKnown
    sequence+=1;lastPower=time.monotonic();powerAwake=awake;powerKnown=known
    a.send('screen.power',sequence=sequence,available=True,known=known,awake=awake)
    t.wait_for(lambda:a.diag()['screen_power']['sequence']==sequence)
   a.open_navigation();before=a.diag();pid=a.process.pid
   power(False);d=a.diag()
   assert d['page_visible'] and d['visible'] and not d['input_focused'] and not d['display_visible']
   assert d['navigation_open'] and d['session']==before['session'] and d['scroll_y']==before['scroll_y']
   a.tap(w-10,60);assert a.diag()['navigation_open']
   power(True);d=a.diag();assert d['navigation_open'] and d['display_visible']
   a.send('screen.power',sequence=1,available=True,known=True,awake=False)
   assert not a.diag()['screen_power']['blocked']
   a.tap(w-10,60);b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['keys'])
   def key(value):
    e=a.diag()['editor'];k=next(k for k in e['keys'] if k['key']==value);s=e['coordinate_space']
    a.tap(round(k['x']*w/s['width']),round(k['y']*h/s['height']))
   for c in 'ni':key(c)
   t.wait_for(lambda:a.diag()['editor']['preedit_length']==2)
   before=a.diag()['editor'];power(False)
   key('a');after=a.diag()['editor']
   assert after['active'] and after['preedit_length']==2 and not after['ocr_active']
   assert after['keyboard_session']==before['keyboard_session'] and after['keys']==before['keys']
   power(True);after=a.diag()['editor']
   assert after['session']==before['session'] and after['preedit_length']==2 and after['keys']==before['keys']
   power(False,False);assert not a.diag()['input_focused']
   power(True);assert a.diag()['display_visible']
   # Expired monitor data is independent of a perfectly valid window focus.
   renew=False;time.sleep(1.6);d=a.diag();assert d['screen_power']['blocked'] and not d['input_focused']
   renew=True
   power(True);assert a.diag()['display_visible']
   # Enter preserves unfinished letters; Done first selects a candidate when
   # preedit exists, so it is not an immediate-close action in that state.
   key('enter');t.wait_for(lambda:a.diag()['editor']['preedit_length']==0)
   a.editor_action('done');t.wait_for(lambda:not a.diag()['editor']['active'])
   t.wait_for(lambda:not a.diag()['surface_motion'])
   a.attachment_action('camera');t.wait_for(lambda:a.diag()['view']=='camera')
   before=a.diag()['camera_generation'];power(False)
   stopped=a.diag()['camera_generation'];assert stopped>before
   # A queued shutter touch cannot start a capture or rebuild this workspace.
   b=a.diag()['camera_controls']['shutter'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   assert a.diag()['view']=='camera' and a.diag()['camera_generation']==stopped
   power(True);assert a.diag()['view']=='camera' and a.diag()['camera_generation']>stopped
   power(False);a.send('visibility',visible=False);stopped=a.diag()['camera_generation']
   power(True);assert not a.diag()['display_visible'] and a.diag()['camera_generation']==stopped
   assert a.process.pid==pid
   print('SCREEN_POWER_RETAINS_DRAWER_EDITOR_PREEDIT_AND_GATES_INPUT_OCR_CAMERA',w,h,flush=True)
  except BaseException:
   out=t.ROOT/'.work/screen-power';out.mkdir(parents=True,exist_ok=True)
   (out/('lvgl-failure-'+str(w)+'.json')).write_text(json.dumps(a.diag(),ensure_ascii=False,indent=2))
   raise
  finally:a.close()
