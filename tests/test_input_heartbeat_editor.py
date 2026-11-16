#!/usr/bin/env python3
"""Touch expiry cancels contacts while retaining an otherwise focused editor."""
import os,pathlib,tempfile,threading,time
import test_lvgl_app as t

t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
class Heartbeat:
 def __init__(self,a):self.a=a;self.sequence=0;self.stop=threading.Event();self.thread=None
 def pulse(self):
  self.sequence+=1;self.a.send('input.focus',sequence=self.sequence,active=True,displayActive=True)
 def start(self):
  self.stop.clear();self.pulse()
  def run():
   while not self.stop.wait(.06):self.pulse()
  self.thread=threading.Thread(target=run);self.thread.start()
 def pause(self):
  self.stop.set()
  if self.thread:self.thread.join();self.thread=None

def run(w,h):
 with tempfile.TemporaryDirectory(prefix='penchat-heartbeat-editor-') as folder:
  a=t.Native(BINARY,folder,official_adapter=False,screen={'width':w,'height':h})
  beat=Heartbeat(a)
  try:
   beat.start()
   def e():return a.diag()['editor']
   def key(name,down=None):
    k=next(k for k in e()['keys'] if k['key']==name)
    if down is None:a.tap(k['x'],k['y'])
    else:a.send('touch',x=k['x'],y=k['y'],pressed=down,points=[{'id':71,'x':k['x'],'y':k['y']}] if down else [])
   def menu():
    k=next(k for k in e()['keys'] if k['key']=='language');a.long_press(k['x'],k['y'])
    t.wait_for(lambda:e()['layout_menu'] and not e()['layout_hold'])
   def snapshot():return {k:e()[k] for k in ['session','keyboard_session','build_count','revision','layout','preedit_length','candidate_count']}
   def expire():
    beat.pause();time.sleep(.38);t.wait_for(lambda:not a.diag()['input_focused'])
    assert a.diag()['visible'] and e()['active'] and not a.diag()['keyboard']
   def resume():beat.start();t.wait_for(lambda:a.diag()['input_focused'])
   b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
   t.wait_for(lambda:e()['active'] and e()['keys'] and not a.diag()['surface_motion'])
   menu();before=snapshot();expire()
   assert e()['layout_menu'] and snapshot()==before
   resume();assert e()['layout_menu'] and snapshot()==before
   a.editor_action('layout:0');t.wait_for(lambda:not e()['layout_menu'] and e()['layout']=='zh26')
   for c in 'ni':key(c)
   t.wait_for(lambda:e()['preedit_length']==2 and e()['candidate_count'] and not e()['pending'])
   before=snapshot();expire();assert snapshot()==before
   resume();assert snapshot()==before
   # A held key never becomes a click when its release crosses expiry.
   key('q',True);expire();key('q',False);resume()
   assert e()['preedit_length']==2
   # Real focus/background loss retains the normal full cancellation path.
   menu();a.send('visibility',visible=False)
   assert not e()['layout_menu'] and not e()['layout_hold']
   a.send('visibility',visible=True);beat.pulse();t.wait_for(lambda:a.diag()['input_focused'])
   assert not e()['layout_menu'] and e()['preedit_length']==2
   key('enter');t.wait_for(lambda:not e()['preedit_length'])
   a.editor_action('done');t.wait_for(lambda:not e()['active'])
   assert a.diag()['draft_length']==2 and not a.read_messages()
   print('EDITOR_HEARTBEAT_GAP_RETAINS_POPUP_QUERY_AND_TREE_CANCELS_TOUCH',w,h,flush=True)
  finally:beat.pause();a.close()

if __name__=='__main__':
 for size in [(960,266),(640,260),(568,1210)]:run(*size)
