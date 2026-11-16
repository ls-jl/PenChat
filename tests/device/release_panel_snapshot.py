"""Own settings page under the system panel; explicit system edge gestures."""
import time

def run(d):
 d.settings();before=d.state();pid=d.pid();d.capture('panel-before')
 def drag(y,end):
  d.input_release();coords=(*d.raw(d.w//2,y),*d.raw(d.w//2,end))
  d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+' '.join(map(str,coords))+' 20')
 drag(2,d.h//2);d.wait(lambda:d.state()['system_overlay'].get('panel_visible',False))
 time.sleep(.4);d.system_capture('panel-retained-page')
 assert not d.state()['display_visible'] and not d.state()['touch_input']['active']
 assert d.sh('find '+d.data+' -maxdepth 1 -name "overlay_*.png" | wc -l')=='1'
 drag(d.h-8,d.h//3)
 d.wait(lambda:not d.state()['system_overlay'].get('panel_visible',False))
 d.wait(lambda:d.state()['display_visible'] and d.state()['input_focused'])
 assert d.pid()==pid and d.state()['view']==before['view']
 assert d.sh('find '+d.data+' -maxdepth 1 -name "overlay_*.png" | wc -l')=='0'
 d.capture('panel-returned')
 return {'same_native_pid':True,'plane_yielded':True,'touch_yielded':True,'one_temporary_frame':True,'frame_removed':True,'page_preserved':True}
