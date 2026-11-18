"""Explicitly owned official-IME surface, with observed controller coordinates."""
import json
import time
import tempfile
import pathlib


def official_visual(d, name):
  if getattr(d, 'official_readback_probe', False):
    state = d.state()
    editor = state.get('editor', {})
    owned_fullscreen = (state.get('keyboard') and editor.get('active') and
                        editor.get('official') and not editor.get('docked') and
                        not state.get('system_overlay', {}).get('panel_visible', False))
    from release_official_readback import owned_fullscreen as verified_fullscreen
    if owned_fullscreen and not state.get('page_visible') and not verified_fullscreen(state,getattr(d,'owned_official_request',None)):
      # Full-screen Falcon editing may hide our page and own the app focus.
      # Observe it through the SDK; keep primary readback's stricter ownership
      # rules for retained native editing, where exact boundary pixels matter.
      return d.system_capture(name)
    from release_official_readback import capture
    return capture(d, name)
  return d.system_capture(name)


def controller_command(d):
  """An optional append-only local file survives tool/PTY reconnection."""
  path = getattr(d, 'official_controller_file', None)
  if path is None:
    return input().strip().split()
  index = getattr(d, '_official_controller_index', 0)
  deadline = time.monotonic()+600
  while time.monotonic() < deadline:
    lines = path.read_text().splitlines() if path.exists() else []
    if len(lines) > index:
      d._official_controller_index = index+1
      command = lines[index].strip().split()
      assert command, 'Empty official controller command'
      return command
    time.sleep(.2)
  raise TimeoutError('Official controller command timed out; restoring isolated data')


def set_default(d, enabled):
  d.chat();d.settings()
  d.tap(d.reveal(lambda s:s['settings']['rows']['keyboard']['bounds']))
  d.wait(lambda:d.state()['view']=='keyboard_settings')
  row=lambda:d.state()['settings']['rows']['prefer_official_keyboard']
  d.tap(d.reveal(lambda s:s['settings']['rows']['prefer_official_keyboard']['bounds']))
  d.wait(lambda:row()['checked'] is enabled)
  d.chat();old=d.pid();d.close()
  if enabled:
    from refresh_bottom_acceptance import APP
    with tempfile.TemporaryDirectory(prefix='penchat-official-restart-') as folder:
      path=pathlib.Path(folder)/'restore.json'
      path.write_text(json.dumps({'appid':APP['appid'],'target':APP['version'],'snapshot':{
        'session':'refresh-bottom','draft':d.official_fake_text,'preferences':{},'scroll':1800,'offset':0,'following':False}}))
      d.sh('mkdir -p '+d.data+'/.updates');d.push(path,d.data+'/.updates/restore.json')
  d.launch();assert d.pid()!=old
  assert d.state()['editor']['official_preference']['saved'] is enabled


def open_owned(d):
  text=d.official_fake_text
  s=d.chat();assert s['draft_length']==len(text)
  if s['composer_hidden']:
    assert s['to_bottom_visible']
    half=(22*s['ui_scale']['applied']+50)//100
    d.touch(s['to_bottom_x']+half,s['to_bottom_y']+half)
    d.wait(lambda:not d.state()['composer_hidden'] and d.state()['at_latest_bottom'])
    s=d.state()
  auto = getattr(d, 'official_default_probe', False) and not s['editor']['active']
  d.tap(s['composer_text_bounds']);d.wait(lambda:d.state()['editor']['active'])
  if auto:
    d.wait(lambda:d.state()['keyboard'])
    e=d.state()['editor'];assert e['official_preference']['saved'] and e['text_visible']
    return text,e,e['session'],e['length']
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
  return text, e, session, length


def provider_singleline(d):
  d.chat();d.settings()
  d.tap(d.reveal(lambda s:s['settings']['rows']['models']['bounds']))
  d.wait(lambda:d.state()['view']=='provider')
  d.tap(d.reveal(lambda s:s['page']['actions']['provider-open:p']))
  d.wait(lambda:'field:name' in d.state()['page']['actions'])
  d.tap(d.reveal(lambda s:s['page']['actions']['field:name']))
  d.wait(lambda:d.state()['keyboard'])
  e=d.state()['editor'];assert e['active'] and e['official'] and not e['multiline']
  session=e['session'];length=e['length']
  image=official_visual(d,'provider-singleline-before')
  print(json.dumps({'event':'provider_singleline_owned_ready','image':str(image),'session':session,'length':length}),flush=True)
  def tap_owned(command, expected):
    assert len(command)==3 and command[0]==expected
    s=d.state();assert s['keyboard'] and s['editor']['session']==session
    assert not s['system_overlay'].get('panel_visible',False)
    x,y=map(int,command[1:]);assert 0<=x<d.w and 0<=y<d.h
    rx,ry=d.raw(x,y);d.input_release()
    d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry)+' 2>&1')
  tap_owned(controller_command(d),'official-delete');time.sleep(1)
  assert d.state()['editor']['length']==length-1
  image=official_visual(d,'provider-singleline-after-delete')
  print(json.dumps({'event':'provider_singleline_confirm_ready','image':str(image),'length':length-1}),flush=True)
  tap_owned(controller_command(d),'official-confirm')
  d.wait(lambda:not d.state()['keyboard'] and not d.state()['editor']['active'])
  d.wait(lambda:d.state()['input_focused'] and d.state()['touch_input']['active'])
  assert d.state()['provider_workspace']['dirty']
  from release_lifecycle import free_back_start
  x,y=free_back_start(d.state(),d.w,d.h,inner=True);d.drag(x,y,min(d.w-1,x+max(100,d.w/4)),y)
  d.wait(lambda:d.state()['context_menu']['kind']=='choice:provider_exit')
  d.tap(d.state()['context_menu']['choices']['discard']['bounds'])
  d.wait(lambda:not d.state()['provider_workspace']['active'])
  return {'default_route':True,'singleline':True,'delete_mirrored':True,'confirm_returned_to_original_form':True,'draft_changes_discarded_without_provider_save':True}


def run(d):
  if getattr(d, 'official_default_probe', False) and not getattr(d, 'official_readback_probe', False):
    set_default(d, True)
  text, e, session, length = open_owned(d)
  s = d.state()
  # keyboard ACK precedes the Falcon editor's first painted frame.
  time.sleep(1)
  image=official_visual(d, 'owned-official-ime-before-cancel')
  (d.out/'owned-official-before.json').write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n')
  (d.out/'owned-official-post-capture.json').write_text(json.dumps(d.state(),ensure_ascii=False,indent=2)+'\n')
  print(json.dumps({'event':'controller_owned_official_ready','image':str(image),
    'unique_fake_text':text,'editor_session':session},ensure_ascii=False),flush=True)
  command=controller_command(d)
  while command==['official-recapture']:
    time.sleep(.5);image=official_visual(d, 'owned-official-ime-before-cancel')
    (d.out/'owned-official-post-capture.json').write_text(json.dumps(d.state(),ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'event':'controller_owned_official_recaptured','image':str(image)},ensure_ascii=False),flush=True)
    command=controller_command(d)
  assert len(command) in (3,4) and command[0]=='official-delete','Delete at the observed Backspace key'
  delete_count=int(command[3]) if len(command)==4 else 1
  assert 1<=delete_count<=min(3,length)
  current=d.state();assert current['keyboard'] and current['editor']['session']==session
  assert not current['system_overlay'].get('panel_visible',False)
  px,py=map(int,command[1:3]);assert 0<=px<d.w and 0<=py<d.h
  rx,ry=d.raw(px,py);d.input_release()
  for _ in range(delete_count):
    d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry)+' 2>&1')
    time.sleep(.15)
  time.sleep(1);d.input_release();changed_image=official_visual(d, 'owned-official-ime-after-delete')
  changed=d.state()
  if e['docked']:
    assert changed['editor']['length']==length-delete_count,'Inline official edit did not reach the native owned field'
  else:
    assert changed['editor']['length']==length,'Legacy full-screen edit must remain outside native snapshot until its result'
  print(json.dumps({'event':'controller_owned_official_changed','image':str(changed_image),
    'deleted_characters':delete_count,'native_length':changed['editor']['length']},ensure_ascii=False),flush=True)
  command=controller_command(d)
  assert len(command)==3 and command[0]=='official-cancel','Only the observed owned IME cancel/return target is accepted'
  # This guard is exclusive to the explicit system-editor request above. A
  # full-screen official editor may legitimately hide the native page. Do not
  # weaken ordinary native foreground checks for it.
  current=d.state();assert current['keyboard'] and current['editor']['active']
  assert current['editor']['session']==session and current['editor']['length']==(length-delete_count if e['docked'] else length)
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
    'deleted_characters':delete_count,
    'distinguishable_edit':'Deleted committed ASCII characters; cancel restores original '+str(length)+'-character snapshot',
    'visual_approval':'pending'}
  if getattr(d, 'official_confirm_probe', False):
    text2, e2, session2, length2 = open_owned(d)
    time.sleep(1)
    image=official_visual(d, 'owned-official-reopen-before-confirm')
    print(json.dumps({'event':'controller_owned_official_reopen_ready','image':str(image),
      'unique_fake_text':text2,'editor_session':session2}),flush=True)
    command=controller_command(d)
    assert len(command)==3 and command[0]=='official-delete'
    def owned_tap(command):
      current=d.state()
      assert current['keyboard'] and current['editor']['session']==session2
      assert not current['system_overlay'].get('panel_visible',False)
      px,py=map(int,command[1:]);assert 0<=px<d.w and 0<=py<d.h
      rx,ry=d.raw(px,py);d.input_release()
      d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry)+' 2>&1')
    owned_tap(command);time.sleep(1)
    image=official_visual(d, 'owned-official-reopen-after-delete')
    changed=d.state()
    assert changed['editor']['length']==(length2-1 if e2['docked'] else length2)
    print(json.dumps({'event':'controller_owned_official_confirm_ready','image':str(image),
      'expected_visible_text':text2[:-1]}),flush=True)
    command=controller_command(d)
    assert len(command)==3 and command[0]=='official-confirm'
    owned_tap(command);d.wait(lambda:not d.state()['keyboard'])
    d.wait(lambda:d.state()['visible'] and d.state()['input_focused'] and d.state()['touch_input']['active'])
    final=d.state()
    assert not final['editor']['active']
    assert final['draft_length']==length2-1
    d.capture('owned-official-confirm-retains-edit')
    result.update(reopen_confirm_preserves_edit=True,confirm_editor_session=session2,
      confirm_length=final['draft_length'])
  if getattr(d, 'official_readback_probe', False):
    result['provider_singleline']=provider_singleline(d)
  if getattr(d, 'official_default_probe', False):
    if d.state()['editor']['active']:
      d.editor_action('done');d.wait(lambda:not d.state()['editor']['active'])
    set_default(d, False)
    s=d.chat();d.tap(s['composer_text_bounds']);d.wait(lambda:d.state()['editor']['active'])
    assert not d.state()['keyboard'] and not d.state()['editor']['official_preference']['saved']
    result.update(default_enabled_persisted=True, default_disabled_persisted=True, default_off_routes_native=True)
  (d.out/'owned-official-results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
  return result
