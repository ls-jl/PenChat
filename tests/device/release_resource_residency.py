"""Real control-center round trips with retained model/plane and gated head key."""
import json,re,time

def planes(d):
 if d.s6:return None
 raw=d.sh('cat /sys/kernel/debug/dri/0/state')
 found={}
 for block in re.split(r'(?=^plane\[)',raw,flags=re.M):
  if 'allocated by = miniapp' not in block and 'allocated by = penchat-lvgl' not in block:continue
  owner='native' if 'allocated by = penchat-lvgl' in block else 'falcon'
  found[owner]={'id':int(re.search(r'plane\[(\d+)\]',block)[1]),'fb':int(re.search(r'\n\s*fb=(\d+)',block)[1])}
  # raw mutable zpos is provided by modetest on some kernels; normalized order
  # is not synthesized when the driver reports an uninformative zero for both.
  found[owner]['normalized_zpos']=int(re.search(r'normalized-zpos=(\d+)',block)[1])
 assert set(found)=={'native','falcon'},found
 return found

def run(d):
 d.chat()
 def ready():
  s=d.state();r=s['scanner']
  if r.get('type') in ('error','fatal'):raise AssertionError('Scanner initialization: '+json.dumps(r))
  return s if r.get('model_ready') and r.get('type')=='ready' else None
 s=d.wait(ready,90);pid=d.pid();worker=s['scanner']['worker_pid'];prepared=s['scanner']['model_prepared_us'];requests=s['scanner']['model_prepare_requests'];before=planes(d)
 if not d.s6:
  stack=s['display_stack'];assert stack[0]!=stack[1] and stack[2]<stack[3],stack
 def drag(start,end):
  d.input_release();coords=(*d.raw(d.w//2,start),*d.raw(d.w//2,end))
  d.sh('PENCHAT_TOUCH_DEVICE='+d.event+' '+d.helper+' '+' '.join(map(str,coords))+' 20')
 rows=[]
 key='/dev/input/by-path/platform-led_control-event' if d.s6 else '/dev/input/by-path/led_control'
 for cycle in range(3):
  drag(2,d.h//2);d.wait(lambda:d.state()['system_overlay'].get('panel_visible',False))
  d.wait(lambda:d.state()['scanner'].get('overlay_paused',False) and d.state()['scanner'].get('overlay_observing',False))
  covered=d.state();assert covered['display_visible'] and not covered['touch_input']['active'] and covered['page_visible']
  assert not d.sh('find '+d.data+' -maxdepth 1 -name "overlay_*.png"')
  observed=covered['scanner'].get('observed_key_edges',0)
  scan=covered['scan_id'];d.sh(d.key_helper+' '+key+' 134 1 120 100');time.sleep(.2)
  now=d.state();assert now['scan_id']==scan and now['view']==covered['view']
  assert now['scanner']['worker_pid']==worker and now['scanner']['model_prepare_requests']==requests
  d.capture('retained-panel-'+str(cycle));d.system_capture('system-panel-'+str(cycle))
  drag(d.h-8,d.h//3);d.wait(lambda:not d.state()['system_overlay'].get('panel_visible',False) and d.state()['input_focused'])
  d.wait(lambda:not d.state()['scanner'].get('overlay_paused',False))
  returned=d.state();assert returned['scanner'].get('observed_key_edges',0)>=observed+2
  assert d.pid()==pid and returned['scanner']['worker_pid']==worker
  assert returned['scanner']['model_prepared_us']==prepared and returned['scanner']['model_prepare_requests']==requests
  after=planes(d)
  if before:
   assert after['native']['id']==before['native']['id']
   stack=returned['display_stack'];assert stack[2]<stack[3],stack
  rows.append({'cycle':cycle,'same_pid':True,'same_worker':True,'same_model_preparation':True,'head_press_did_not_start_scan':True,'planes_before':before,'planes_after':after})
 timings=[]
 for cycle in range(3):
  d.tap(d.state()['composer_text_bounds']);d.wait(lambda:d.state()['editor']['active'])
  d.wait(lambda:not d.state()['surface_motion']);editor=d.state()['editor'];timing=editor['timing']
  expected=180 if d.h>d.w else 120
  elapsed=timing['entry_finished']-timing['animation']
  assert timing['duration_ms']==expected and timing['animation']<=timing['first_frame']<=timing['entry_finished'],timing
  # Async OCR/capability completion can rebuild an entering keyboard. That
  # path intentionally uses its current displacement to resolve the remaining
  # duration; only an uninterrupted entry has the original wall-time bound.
  rebuilt=editor['build_count']>1
  assert elapsed>0 and (rebuilt or elapsed>=expected-5),(timing,editor['build_count'])
  d.editor_action('done');d.wait(lambda:not d.state()['surface_motion'])
  closed=d.state()['editor']['timing'];assert closed['exit_finished']-closed['exit_started']>=expected-5,closed
  closed.update(entry_elapsed_ms=elapsed,entry_build_count=editor['build_count'],
                entry_duration_mode='remaining-distance-after-rebuild' if rebuilt else 'full')
  timings.append(closed);d.wait(ready,90)
 return {'cycles':rows,'keyboard_timings':timings,'same_native_pid':pid,'same_worker_pid':worker,'snapshot_files_created':False,'model_prepare_requests':requests}
