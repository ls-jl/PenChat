"""Observed official keyboard targets inside the isolated app only."""
import json,time
from release_official_surface import controller_command, official_visual

def run(d):
 assert d.serial in ('MEB0400008900794','MEA2300009007113','JDA0700006552216')
 assert d.s6 or getattr(d,'official_controller_file',None), 'Observe each RK official editor before choosing system controls'
 d.settings();d.tap(d.reveal(lambda s:s['settings']['rows']['keyboard']['bounds']))
 d.wait(lambda:d.state()['view']=='keyboard_settings')
 if not d.state()['editor']['official_shortcut']['saved']:
  d.tap(d.reveal(lambda s:s['settings']['rows']['quick_official_keyboard']['bounds']))
  d.wait(lambda:d.state()['editor']['official_shortcut']['saved'])
 s=d.chat();pid=d.pid();d.tap(s['composer_text_bounds']);d.wait(lambda:d.state()['editor']['active'])
 if d.state()['editor']['chinese']:d.editor_key('language')
 for key in 'qa':d.editor_key(key)
 assert d.state()['editor']['length']==2
 d.capture('direct-system-action');assert 'official' in d.state()['editor']['actions']
 d.editor_action('official');d.wait(lambda:d.state()['keyboard']);time.sleep(.7)
 image=official_visual(d,'official-return-target')
 print(json.dumps({'stage':'official-return-target','image':str(image)},ensure_ascii=False),flush=True)
 command=controller_command(d) if getattr(d,'official_controller_file',None) else ['official-return','34','18'];assert len(command)==3 and command[0]=='official-return'
 def tap_system(command):
  assert d.state()['keyboard'];x,y=map(int,command[1:]);assert 0<=x<d.w and 0<=y<d.h
  d.input_release();rx,ry=d.raw(x,y);d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry))
 tap_system(command);d.wait(lambda:not d.state()['keyboard'] and not d.state()['editor']['active'])
 d.wait(lambda:d.state()['input_focused'] and d.state()['touch_input']['active'])
 assert d.state()['draft_length']==2 and not d.state()['composer_hidden']
 d.capture('system-return-no-native-keyboard')
 d.settings();d.tap(d.reveal(lambda s:s['settings']['rows']['keyboard']['bounds']))
 d.wait(lambda:d.state()['view']=='keyboard_settings')
 d.tap(d.reveal(lambda s:s['settings']['rows']['prefer_official_keyboard']['bounds']))
 d.wait(lambda:d.state()['editor']['official_preference']['saved'])
 d.chat();d.tap(d.state()['composer_text_bounds']);d.wait(lambda:d.state()['keyboard']);time.sleep(.5)
 image=official_visual(d,'default-official-return-target')
 print(json.dumps({'stage':'default-official-return-target','image':str(image)},ensure_ascii=False),flush=True)
 command=controller_command(d) if getattr(d,'official_controller_file',None) else ['official-return','34','18'];assert len(command)==3 and command[0]=='official-return'
 tap_system(command);d.wait(lambda:not d.state()['keyboard'] and not d.state()['editor']['active'])
 d.wait(lambda:d.state()['input_focused'] and d.state()['touch_input']['active'])
 assert d.state()['draft_length']==2 and not d.state()['composer_hidden']
 d.capture('default-system-return-no-native-keyboard')
 d.tap(d.state()['composer_text_bounds']);d.wait(lambda:d.state()['keyboard']);time.sleep(.5)
 image=official_visual(d,'official-confirm-target')
 print(json.dumps({'stage':'official-confirm-target','image':str(image)},ensure_ascii=False),flush=True)
 command=controller_command(d) if getattr(d,'official_controller_file',None) else ['official-confirm','592','222']
 assert len(command)==3 and command[0]=='official-confirm'
 tap_system(command);d.wait(lambda:not d.state()['keyboard'] and not d.state()['editor']['active'])
 d.wait(lambda:d.state()['input_focused'] and d.state()['touch_input']['active'])
 assert d.state()['draft_length']==2 and not d.state()['composer_hidden']
 d.capture('system-confirm-composer-retained')
 assert d.pid()==pid
 return {'direct_system_button':True,'default_system_route':True,'normal_return_no_native_fallback':True,'draft_preserved':True,'system_confirm_retains_composer':True,'completed_composer_visible':True,'native_pid_unchanged':True,'sent_messages':0}
