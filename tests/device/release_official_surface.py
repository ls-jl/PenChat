"""Explicitly owned official-IME surface, with observed controller coordinates."""
import json
import time


def run(d):
  text=d.official_fake_text
  s=d.chat();assert s['draft_length']==len(text)
  if s['composer_hidden']:
    assert s['to_bottom_visible']
    half=(22*s['ui_scale']['applied']+50)//100
    d.touch(s['to_bottom_x']+half,s['to_bottom_y']+half)
    d.wait(lambda:not d.state()['composer_hidden'] and d.state()['at_latest_bottom'])
    s=d.state()
  d.tap(s['composer_text_bounds']);d.wait(lambda:d.state()['editor']['active'])
  e=d.state()['editor'];b=e['text_bounds'];c=e['coordinate_space']
  d.touch((b[2]-3)*d.w/c['width'],(b[1]+b[3])*d.h/(2*c['height']))
  d.wait(lambda:d.state()['editor']['cursor']==len(text))
  e=d.state()['editor'];session=e['session'];length=e['length']
  language=next(k for k in e['keys'] if k['key']=='language');c=e['coordinate_space']
  d.input_release();x,y=d.raw(language['x']*d.w/c['width'],language['y']*d.h/c['height'])
  assert d.state()['input_focused'] and d.state()['touch_input']['active']
  d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' --hold '+str(x)+' '+str(y)+' 700 2>&1')
  d.wait(lambda:d.state()['editor']['layout_menu'])
  d.editor_action('official');d.wait(lambda:d.state()['keyboard'])
  s=d.state();assert s['editor']['active'] and s['editor']['session']==session
  # keyboard ACK precedes the Falcon editor's first painted frame.
  time.sleep(1)
  image=d.system_capture('owned-official-ime-before-cancel')
  (d.out/'owned-official-before.json').write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n')
  (d.out/'owned-official-post-capture.json').write_text(json.dumps(d.state(),ensure_ascii=False,indent=2)+'\n')
  print(json.dumps({'event':'controller_owned_official_ready','image':str(image),
    'unique_fake_text':text,'editor_session':session},ensure_ascii=False),flush=True)
  command=input().strip().split()
  while command==['official-recapture']:
    time.sleep(.5);image=d.system_capture('owned-official-ime-before-cancel')
    (d.out/'owned-official-post-capture.json').write_text(json.dumps(d.state(),ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'event':'controller_owned_official_recaptured','image':str(image)},ensure_ascii=False),flush=True)
    command=input().strip().split()
  assert len(command)==3 and command[0]=='official-delete','First delete one committed ASCII character at the observed Backspace key'
  current=d.state();assert current['keyboard'] and current['editor']['session']==session
  assert not current['system_overlay'].get('panel_visible',False)
  px,py=map(int,command[1:]);assert 0<=px<d.w and 0<=py<d.h
  rx,ry=d.raw(px,py);d.input_release()
  d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry)+' 2>&1')
  time.sleep(1);d.input_release();changed_image=d.system_capture('owned-official-ime-after-delete')
  changed=d.state()
  if e['docked']:
    assert changed['editor']['length']==length-1,'Inline official edit did not reach the native owned field'
  else:
    assert changed['editor']['length']==length,'Legacy full-screen edit must remain outside native snapshot until its result'
  print(json.dumps({'event':'controller_owned_official_changed','image':str(changed_image),
    'expected_visible_text':text[:-1],'native_length':changed['editor']['length']},ensure_ascii=False),flush=True)
  command=input().strip().split()
  assert len(command)==3 and command[0]=='official-cancel','Only the observed owned IME cancel/return target is accepted'
  # This guard is exclusive to the explicit system-editor request above. A
  # full-screen official editor may legitimately hide the native page. Do not
  # weaken ordinary native foreground checks for it.
  current=d.state();assert current['keyboard'] and current['editor']['active']
  assert current['editor']['session']==session and current['editor']['length']==(length-1 if e['docked'] else length)
  assert not current['system_overlay'].get('panel_visible',False)
  px,py=map(int,command[1:]);assert 0<=px<d.w and 0<=py<d.h
  rx,ry=d.raw(px,py);d.input_release()
  receipt=d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry)+' 2>&1')
  time.sleep(.4);d.input_release();d.wait(lambda:not d.state()['keyboard'])
  s=d.state();assert s['editor']['active'] and s['editor']['session']==session and s['editor']['length']==length
  d.wait(lambda:d.state()['visible'] and d.state()['input_focused'] and d.state()['touch_input']['active'])
  d.capture('owned-official-ime-cancel-preserved')
  result={'owned_explicit_session':session,'unique_fake_text':text,'initial_length':length,
    'observed_cancel_point':[px,py],'receipt':receipt,'cancel_preserves_input':True,
    'native_foreground_restored':True,'page_visible_during_official':current['page_visible'],
    'distinguishable_edit':'Deleted one committed ASCII char; cancel restores original '+str(length)+'-character snapshot',
    'visual_approval':'pending'}
  (d.out/'owned-official-results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
  return result
