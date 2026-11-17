#!/usr/bin/env python3
"""Explicit isolated long-draft, sleep/wake and foreground-soak acceptance."""
import argparse,json,pathlib,re,sys,time,types,signal
import release_acceptance as r

ROOT=pathlib.Path(__file__).resolve().parents[2]
def run(d,cycles,seconds):
 def state():return d.state()
 def power():
  d.sh(d.key_helper+' '+power_node+' 116 1 100 300')
 def light():return int(d.sh('cat '+backlight+'/actual_brightness'))
 def interactive():
  s=state();return s if s['visible'] and s['page_visible'] and s['window_focused'] and s['touch_input']['active'] and not s['keyboard'] else None
 def record():
  (d.out/'stability-progress.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')
 # Verify the actual power event and choose an existing by-path link to it.
 raw=d.sh('cat /proc/bus/input/devices');event=None
 for block in raw.split('\n\n'):
  if not any('Name="'+name+'"' in block for name in ['rk805 pwrkey','axp2101-pek']):continue
  key=re.search(r'B: KEY=(.*)',block);handler=re.search(r'Handlers=.*\b(event\d+)\b',block)
  assert key and handler
  words=key[1].split();bits=32 if d.s6 else 64
  value=sum(int(v,16)<<(bits*i) for i,v in enumerate(reversed(words)))
  assert value&(1<<116),'Discovered node does not advertise KEY_POWER'
  event='/dev/input/'+handler[1]
 assert event,'No verified power-key node'
 paths=d.sh('printf "%s\\n" /dev/input/by-path/*').splitlines()
 power_node=next((p for p in paths if d.sh('readlink -f '+p)==event),None)
 assert power_node,'No verified by-path alias for power key'
 backlight='/sys/class/backlight/'+('dcs-backlight' if d.serial=='7G12000010905426' else 'backlight')
 d.chat();d.wait(interactive);pid=d.pid();first=state()
 def scanner(s):
  q=s['scanner'];return {k:q.get(k) for k in ['worker_pid','model_ready','model_prepared_us']}
 # Allow a cold prepared model to settle once, before comparing wake cycles.
 d.wait(lambda:state()['scanner'].get('model_ready',False),90)
 model=scanner(state());evidence={'suite':'input-stability','native_pid':pid,'power_node':power_node,'model_before':model,'cycles':[],'observations':[],'requested_cycles':cycles,'requested_observation_seconds':seconds}
 record()
 for i in range(cycles):
  before=state();brightness=light();power()
  def covered():
   s=state();dark=light()
   marker=(dark<brightness or not s['window_focused'] or not s['page_visible'] or not s['display_visible'])
   return {'brightness':dark,'visible':s['visible'],'page_visible':s['page_visible'],'window_focused':s['window_focused'],'display_visible':s['display_visible'],'touch_active':s['touch_input']['active']} if marker and not s['touch_input']['active'] else None
  try:
   off=d.wait(covered,10);assert not off['touch_active']
   time.sleep(1)
  finally:
   # Always finish the requested power-key pair before outer data recovery.
   power();d.wait(interactive,30)
  after=state();assert d.pid()==pid,'Native PID changed after wake'
  assert scanner(after)==model,'Model identity/preparation changed after wake'
  assert after['session']==before['session'] and after['draft_length']==before['draft_length']
  evidence['cycles'].append({'cycle':i+1,'sleep_evidence':off,'same_pid':True,'same_model':True,'draft_preserved':True});record()
 d.capture('after-'+str(cycles)+'-wake-cycles')
 start=time.monotonic();deadline=start+seconds;iteration=0
 while time.monotonic()<deadline:
  s=d.chat();assert d.pid()==pid
  if s['composer_hidden']:
   b=s['composer_shortcut']['bounds'];d.touch((b[0]+b[2])/2,(b[1]+b[3])/2);d.touch((b[0]+b[2])/2,(b[1]+b[3])/2)
   d.wait(lambda:not state()['composer_hidden']);s=state()
  d.tap(s['composer_text_bounds']);d.wait(lambda:state()['editor']['active'] and state()['editor']['keys'] and not state()['surface_motion'])
  e=state()['editor']
  if e['chinese']:d.editor_key('language')
  length=state()['editor']['length'];d.editor_key('a');d.wait(lambda:state()['editor']['length']==length+1)
  d.editor_action('done');d.wait(lambda:not state()['editor']['active'])
  s=state();assert not s['composer_hidden'] and d.pid()==pid
  d.wait(lambda:state()['scanner'].get('model_ready',False),30)
  s=state();assert scanner(s)==model
  iteration+=1
  if iteration%6==1:d.capture('foreground-'+str(iteration))
  evidence['observations'].append({'elapsed_seconds':round(time.monotonic()-start,2),'pid':pid,'draft_length':s['draft_length'],'model':scanner(s),'prepare_ipc_requests':s['scanner'].get('model_prepare_requests')})
  record()
  # Keep an ordinary editing workload active without leaving a long tool wait.
  end=min(deadline,time.monotonic()+10)
  while time.monotonic()<end:time.sleep(min(1,end-time.monotonic()))
 evidence['observed_seconds']=round(time.monotonic()-start,2);evidence['iterations']=iteration
 evidence['native_pid_unchanged']=d.pid()==pid;evidence['model_after']=scanner(state());record()
 return evidence

def main():
 def interrupted(signum,frame):raise SystemExit('Interrupted; restoring isolated data: '+str(signum))
 for number in [signal.SIGTERM,signal.SIGINT,signal.SIGHUP]:signal.signal(number,interrupted)
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True,choices=tuple(r.DEVICES));p.add_argument('--package',required=True,type=pathlib.Path);p.add_argument('--run-label',default='devices');p.add_argument('--cycles',type=int,default=20);p.add_argument('--observe-seconds',type=int,default=1800);a=p.parse_args()
 assert a.cycles>=0 and a.observe_seconds>=0
 assert re.fullmatch('[a-z0-9-]+',a.run_label)
 r.OUT=ROOT/'.work/input-stability'/a.run_label
 d=r.ReleaseDevice(a.serial,a.package,['keyboard-review'],True,True);d.discard_image_input=a.serial in ['MEA2300009007113','JDA0700006552216']
 # One private fixture restore file supplies long input; it never touches originals.
 original=d.launch;first=[True]
 def launch(self):
  if first[0] and self.testing_payload_installed:
   first[0]=False
   payload={'appid':r.APP['appid'],'target':r.APP['version'],'snapshot':{'session':'refresh-bottom','draft':'长文测试 English paragraph。'*70,'preferences':{},'scroll':0,'offset':0,'following':True}}
   with __import__('tempfile').TemporaryDirectory(prefix='penchat-stability-draft-') as folder:
    f=pathlib.Path(folder)/'restore.json';f.write_text(json.dumps(payload,ensure_ascii=False));self.sh('mkdir -p '+self.data+'/.updates');self.push(f,self.data+'/.updates/restore.json')
  return original()
 d.launch=types.MethodType(launch,d)
 d.keyboard_review=types.MethodType(lambda self:run(self,a.cycles,a.observe_seconds),d)
 try:d.run()
 except BaseException:
  for name in ['last-exit.json','lvgl-runtime.log','lvgl-runtime.previous.log']:
   try:(d.out/('failure-'+name)).write_text(d.sh('cat '+d.data+'/'+name+' 2>/dev/null || true'))
   except Exception:pass
  raise
if __name__=='__main__':main()
