"""Real inner-edge Back, cancellation and ownership validation on explicit device profiles."""
import hashlib
import json
import time
import zipfile

from release_lifecycle import free_back_start


def run(d):
  assert d.serial in ('7G12000010905426','MEB0400008900794','MEA2300009007113','JDA0700006552216')
  with zipfile.ZipFile(d.package) as package:
    expected = hashlib.sha256(package.read('assets/bin/penchat-lvgl')).hexdigest()
  assert d.sh('sha256sum /proc/'+d.pid()+'/exe').split()[0] == expected
  results = {}
  def save():
    (d.out/'y07-new-native-back-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')

  def gesture(start, end, hold=0, long_press=False):
    before=d.state();assert before['visible'] and before['page_visible'] and before['input_focused'] and before['touch_input']['active']
    d.input_release();before=d.state()
    values=(*d.raw(*start),*d.raw(*end))
    flag=' --drag-hold ' if long_press else ' --move-hold ' if hold else ' '
    suffix=(' 600 '+str(hold)) if long_press else (' 150 '+str(hold)) if hold else ''
    receipt=d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+flag+' '.join(map(str,values))+suffix+' 2>&1')
    time.sleep(.4 if long_press else .7)
    after=d.state() if d.pid() else None
    return before,after,receipt

  def inner(s):
    g=s['back_navigation'];assert g['edge']>=44 and g['slop']>0 and g['distance']>g['slop']
    x,y=free_back_start(s,d.w,d.h,inner=True);assert x==g['edge']-1
    return x,y

  d.chat();d.settings();s=d.state();start=inner(s);g=s['back_navigation']
  end=(start[0]+min(g['distance']-1,max(g['slop']+3,g['distance']//2)),start[1])
  before,after,receipt=gesture(start,end)
  assert after and after['view']=='settings' and after['pid']==before['pid'] and not after['back_navigation']['claimed']
  results['short_cancel']={'start':start,'end':end,'view_unchanged':True,'receipt':receipt}
  save()
  before,after,receipt=gesture(start,(start[0]+g['distance']+20,start[1]))
  assert after and after['view']=='chat' and after['pid']==before['pid']
  results['child_back']={'start':start,'returned_to_chat':True,'same_pid':True,'receipt':receipt}
  save()
  d.chat()

  # The attachment picker overlaps the new Back band and owns its inside drag.
  d.tap(d.state()['attachment_button']);d.wait(lambda:d.state()['attachments'])
  s=d.state();b=s['attachment_actions']['camera'];p=s['popup_bounds'];x=s['back_navigation']['edge']-1;y=(b[1]+b[3])/2
  assert p[1]<=y<=p[3]
  if not p[0]<=x<=p[2]:
    # Landscape popups can begin beyond the native Back band. Verify the
    # actual popup's drag ownership without inventing an edge overlap.
    x=p[0]+min(12,(p[2]-p[0])/4)
  before,after,receipt=gesture((x,y),(min(p[2]-4,x+s['back_navigation']['distance']+16),y))
  assert after and after['pid']==before['pid'] and not after['back_navigation']['claimed']
  assert (after['view']=='chat' and after['attachments']) or after['view']=='camera'
  results['picker_priority']={'start':[x,y],'view_after':after['view'],
    'picker_retained':after['attachments'],'native_back_not_claimed':True,
    'popup_overlaps_back_start':p[0]<=s['back_navigation']['edge']-1<=p[2],'receipt':receipt}
  save()
  if after['view']=='camera':
    camera_start=inner(after)
    _,returned,_=gesture(camera_start,(camera_start[0]+g['distance']+20,camera_start[1]))
    assert returned and returned['view']=='chat'
  else:
    d.touch(d.w-12,d.h/2);d.wait(lambda:not d.state()['attachments'])

  s=d.state();d.tap(s['composer_text_bounds']);d.wait(lambda:d.state()['editor']['active'] and d.state()['editor']['keys'])
  if d.state()['editor']['chinese']:d.editor_key('language')
  e=d.state()['editor'];q=next(k for k in e['keys'] if k['key']=='q');c=e['coordinate_space'];edge=d.state()['back_navigation']['edge']
  x=min(edge-1,(q['hit_bounds'][2]-2)*d.w/c['width']);y=q['y']*d.h/c['height']
  assert (q['hit_bounds'][0]+1)*d.w/c['width']<=x<=edge
  d.touch(x,y);d.wait(lambda:d.state()['editor']['length']==1)
  before,after,receipt=gesture((x,y),(x+g['distance']+16,y))
  assert after and after['editor']['active'] and after['session']==before['session'] and not after['back_navigation']['claimed']
  results['keyboard_priority']={'start':[x,y],'editor_retained':True,'length':after['editor']['length'],'receipt':receipt}
  save()
  for _ in range(after['editor']['length']):d.editor_key('delete')
  d.editor_action('done');d.wait(lambda:not d.state()['editor']['active'])

  s=d.state();x=s['back_navigation']['edge']-1;reader=s['chat_header']['reader_bounds']
  parts=[b for r in s['text_selection']['regions'] for b in r['parts']
    if b[3]>max(b[1],reader[1])+16 and b[1]<reader[3]-16]
  overlapping=[b for b in parts if b[0]<=x<=b[2]]
  assert parts,'Fixture must contain visible readable text'
  part=(overlapping or parts)[0]
  edge_overlap=bool(overlapping)
  if not edge_overlap:x=part[0]+13
  y=max(part[1],reader[1])+13
  before,after,receipt=gesture((x,y),(x+g['distance']+16,y),hold=100,long_press=True)
  assert after and after['view']=='chat' and after['pid']==before['pid'] and not after['back_navigation']['claimed']
  assert after['text_selection']['selected'], 'Reader hold must select text instead of starting Back'
  d.capture('new-back-reader-selection-priority')
  results['read_priority']={'start':[x,y],'selected':True,'same_pid':True,
    'readable_text_overlaps_back_start':edge_overlap,'receipt':receipt}
  save()
  d.touch(d.w-12,d.h/2)

  # The SDK's reserved outer strip is a negative safety case, never a positive
  # native Back entry. Hold until its real focus loss cancels the native drag.
  if d.serial=='7G12000010905426':
    d.settings();s=d.state();outer=free_back_start(s,d.w,d.h)
    before,after,receipt=gesture(outer,(outer[0]+g['distance']+20,outer[1]),hold=1000)
    assert after and after['view']=='settings' and after['pid']==before['pid'] and not after['back_navigation']['claimed']
    trace=after['input_focus_trace']['entries'];events=[e for e in trace if e['time']>=before['diagnostic_time']]
    assert any(e['reason']=='touch_lease_applied_before_cancel' and not e['event_value'] for e in events), 'Negative case did not exercise real focus cancellation'
    results['focus_cancel']={'reserved_outer_start':outer,'view_unchanged':True,'events':events,'receipt':receipt,
      'note':'SDK outer strip remains reserved; not counted as a usable native Back entry'}
    save()
  d.chat();s=d.state();start=inner(s);native=d.pid()
  before,after,receipt=gesture(start,(start[0]+s['back_navigation']['distance']+20,start[1]))
  d.wait(lambda:not d.pid(),10)
  results['root_finish']={'start':start,'native_pid_before':native,'native_exited_without_term':True,'receipt':receipt}
  results['geometry']=g
  (d.out/'y07-new-native-back-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
  return results
