"""Observed Y07 official editing and theme boundary against an isolated local API."""
import pathlib,time,json
import release_official_surface as surface

def run(d):
 assert d.serial=='7G12000010905426' and (d.w,d.h)==(568,1210), 'Observed official-keyboard fixture requires verified Y07 geometry'
 out=d.out
 def rawtap(x,y):
  state=d.state();assert state['page_visible'] and not state['system_overlay'].get('panel_visible',False) and (state['keyboard'] or state['input_focused'])
  rx,ry=d.raw(x,y);d.input_release();d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry))
 def plus():
  s=d.state();b=s['attachment_button'];rawtap((b[0]+b[2])//2,(b[1]+b[3])//2)
 def hold(x,y,ex=None,ey=None):
  d.input_release();rx,ry=d.raw(x,y)
  args='--hold '+str(rx)+' '+str(ry)+' 700' if ex is None else '--drag-hold '+' '.join(map(str,(*d.raw(x,y),*d.raw(ex,ey),650,80)))
  d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+args);time.sleep(.25)
 def action(name):
  b=d.state()['editor']['actions'][name];rawtap((b[0]+b[2])//2,(b[1]+b[3])//2);time.sleep(.3)
 def textpoint():
  b=d.state()['editor']['text_bounds'];return b[0]+6,(b[1]+b[3])//2
 text,e,session,length=surface.open_owned(d);time.sleep(.5);pid=d.pid()
 theme=d.state()['appearance']['theme'];color=255 if theme=='light' else 0
 image=surface.official_visual(d,'official-final-'+theme+'-boundary')
 base=str(image).replace('-keyboard.png','')
 meta=json.loads(pathlib.Path(base+'.raw.json').read_text());pixels=pathlib.Path(base+'.raw').read_bytes()
 state=d.state();start=state['composer_bounds'][3]+1;end=state['display_top_height']
 assert start<=end
 for yy in range(start,end+1):
  for xx in [0,d.w//2,d.w-1]:
   px,py=d.w-1-xx,d.h-1-yy;offset=py*meta['pitch']+px*4
   pixel=pixels[offset:offset+4]
   # Falcon can retain transparent black on its primary plane in dark mode;
   # the native/DRM background is black too. Light mode must be opaque white
   # or the former hole would show the reported black stripe.
   assert pixel[:3]==bytes([color,color,color]) and (theme=='dark' or pixel[3]==255),(xx,yy,list(pixel))

 plus();d.wait(lambda:d.state()['attachments'] and d.state()['keyboard'])
 rawtap(d.w-8,100);d.wait(lambda:not d.state()['attachments']);assert d.state()['keyboard']
 x,y=textpoint();rawtap(x,y);d.wait(lambda:d.state()['editor']['cursor']==0)
 hold(x,y,x+150,y);assert d.state()['editor']['cursor']>0
 x,y=textpoint();hold(x+30,y);d.wait(lambda:d.state()['editor']['menu'])
 d.capture('official-text-selection');surface.official_visual(d,'official-text-selection-keyboard')
 before=d.state()['editor']['selection_end'];grip=d.state()['editor']['selection_handles'][1]['grip']
 coords=(*d.raw(*grip),*d.raw(grip[0]+45,grip[1]));d.input_release();d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+' '.join(map(str,coords)));time.sleep(.3)
 assert d.state()['editor']['selection_end']>before
 action('all');d.wait(lambda:d.state()['editor']['selection_end']==length)
 action('copy');assert d.state()['editor']['clipboard_count']==1
 hold(x+30,y);action('all');action('cut');d.wait(lambda:d.state()['editor']['length']==0)
 x,y=textpoint();rawtap(x,y);d.wait(lambda:d.state()['editor']['menu']);action('paste');d.wait(lambda:d.state()['editor']['length']==length)
 x,y=textpoint();rawtap(x,y);d.wait(lambda:d.state()['editor']['cursor']==0)
 rawtap(368,960);rawtap(425,780);time.sleep(.25);rawtap(38,694)
 d.wait(lambda:d.state()['editor']['length']==length+1)
 d.capture('official-text-pasted-and-typed')
 x,y=textpoint();hold(x+30,y);action('all')
 rawtap(368,960);rawtap(425,780);time.sleep(.25);rawtap(38,694)
 d.wait(lambda:d.state()['editor']['length']==1)
 d.capture('official-text-selected-replacement')
 current=d.state();rawtap(current['send_center_x2']//2,current['send_center_y2']//2)
 d.wait(lambda:len(d.background_server.requests)==1);d.background_server.command('finish')
 d.wait(lambda:not d.state()['generating'] and not d.state()['keyboard'])
 assert d.state()['draft_length']==0 and d.pid()==pid
 d.capture('official-text-sent')
 # Normal system hide must finish editing, not reveal another keyboard.
 b=d.state()['composer_text_bounds'];rawtap((b[0]+b[2])//2,(b[1]+b[3])//2)
 d.wait(lambda:d.state()['keyboard'])
 surface.official_visual(d,'official-hide-target')
 rawtap(530,680)
 d.wait(lambda:not d.state()['keyboard'] and not d.state()['editor']['active'])
 assert not d.state()['composer_hidden']
 d.capture('official-hidden-no-native-fallback')
 # Disable the default through its real setting, then use the direct button.
 d.settings();d.tap(d.reveal(lambda s:s['settings']['rows']['keyboard']['bounds']))
 d.wait(lambda:d.state()['view']=='keyboard_settings')
 d.tap(d.reveal(lambda s:s['settings']['rows']['prefer_official_keyboard']['bounds']))
 d.wait(lambda:not d.state()['editor']['official_preference']['saved'])
 if not d.state()['editor']['official_shortcut']['saved']:
  d.tap(d.reveal(lambda s:s['settings']['rows']['quick_official_keyboard']['bounds']))
  d.wait(lambda:d.state()['editor']['official_shortcut']['saved'])
 d.chat();b=d.state()['composer_text_bounds'];d.tap(b)
 d.wait(lambda:d.state()['editor']['active'] and not d.state()['keyboard'])
 d.capture('native-direct-system-key')
 d.editor_action('official');d.wait(lambda:d.state()['keyboard'])
 rawtap(530,680);d.wait(lambda:not d.state()['keyboard'] and not d.state()['editor']['active'])
 d.capture('direct-system-hidden')
 # Normal shutdown checkpoints the isolated DB; inspect only this fixture.
 d.close()
 import subprocess,sqlite3
 saved=out/'sent-fixture.db';subprocess.run(['adb','-s',d.serial,'pull',d.data+'/penchat.db',str(saved)],check=True,capture_output=True)
 with sqlite3.connect(saved) as db:
  row=db.execute("select output_text from chat_messages where role='user' order by rowid desc limit 1").fetchone()
  assert row and row[0]=='比',row
 saved.unlink()
 return {'caret_tap':True,'caret_drag':True,'select_all':True,'copy_cut_paste':True,'type_after_paste':True,'single_local_send':True,'native_pid_stable':True,'selection_handle_drag':True,'selected_chinese_replacement':True,'saved_message_exact':True,'empty_caret_tap_paste':True,'normal_hide_finishes_editing':True,'direct_system_button':True,'completed_composer_visible':True,'gap_pixels_match_theme':True,'theme':theme}
