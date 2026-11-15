"""Owned official-IME plane attribution using existing read-only debugfs state."""
import json
import pathlib
import shlex
import time

from release_official_surface import open_owned, controller_command


def run(d):
  assert d.serial == '7G12000010905426'
  paths = d.sh('for path in /sys/kernel/debug/dri/*/state; do if test -r "$path"; then printf "%s\\n" "$path"; fi; done').splitlines()
  if not paths:
    return {'diagnostic': 'unavailable', 'reason': 'No readable existing DRM debugfs state; no mount or system changes attempted'}
  boundaries = []
  def snapshot(name):
    start = time.time_ns();before = d.state();states = {}
    for index, path in enumerate(paths):
      assert path.startswith('/sys/kernel/debug/dri/') and path.endswith('/state')
      local = d.out/(name+'-drm-'+str(index)+'.txt')
      local.write_text(d.sh('cat '+shlex.quote(path))+'\n')
      states[path] = str(local)
    image = d.system_capture(name+'-sdk')
    after = d.state()
    record = {'boundary': name, 'start_ns': start, 'end_ns': time.time_ns(),
      'state_files': states, 'sdk_image': str(image), 'native_image': None,
      'native_before': before, 'native_after': after,
      'ownership_source': 'Existing kernel DRM debugfs state: allocated by and plane/FB/CRTC/geometry fields'}
    boundaries.append(record)
    (d.out/'owned-official-drm-boundaries.json').write_text(json.dumps(boundaries,ensure_ascii=False,indent=2)+'\n')
    if name.endswith('after-delete'):
      # Display::screenshot calls lv_refr_now; never trigger that before the
      # real keyboard edit or before its DRM/SDK evidence has been sampled.
      d.capture(name+'-native')
      record['native_image']=str(d.out/(name+'-native.png'))
      (d.out/'owned-official-drm-boundaries.json').write_text(json.dumps(boundaries,ensure_ascii=False,indent=2)+'\n')
    return image
  d.chat();snapshot('official-drm-native-baseline')
  text,e,session,length = open_owned(d)
  time.sleep(1);image = snapshot('official-drm-first-keyboard')
  print(json.dumps({'event':'controller_owned_official_drm_ready','image':str(image),
    'unique_fake_text':text,'editor_session':session}),flush=True)
  command = controller_command(d)
  assert len(command)==3 and command[0]=='official-delete'
  current=d.state();assert current['keyboard'] and current['editor']['session']==session
  assert not current['system_overlay'].get('panel_visible',False)
  x,y=map(int,command[1:]);assert 0<=x<d.w and 0<=y<d.h
  rx,ry=d.raw(x,y);d.input_release()
  d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+str(rx)+' '+str(ry)+' 2>&1')
  time.sleep(1);d.input_release();image=snapshot('official-drm-after-delete')
  assert d.state()['editor']['session']==session and d.state()['editor']['length']==length-1
  print(json.dumps({'event':'owned_official_drm_complete','image':str(image)}),flush=True)
  return {'diagnostic':'three boundaries captured','boundaries':str(d.out/'owned-official-drm-boundaries.json'),
    'delete_mirrored':True,'cancel_confirm':'not attempted; owned cleanup only','physical_display':'not established by SDK capture'}
