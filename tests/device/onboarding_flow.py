"""Native onboarding using physical touchscreen events in isolated user directories."""
import time

def run(c):
 state,tap,wait,capture=c['state'],c['tap'],c['wait'],c['capture']
 def action(key):
  b=state()['onboarding']['actions'][key];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
  wait(lambda:not (state().get('onboarding') or {}).get('busy',False))
 assert state()['view']=='onboarding'
 native=c['pid']();captures=0;performance=[]
 for page in range(7):
  d=state();assert d['onboarding']['step']==page and not d['editor']['active'] and not d['keyboard']
  assert d['onboarding']['renderer']=='software_3d' and not d['onboarding']['render_failed']
  assert 0<d['onboarding']['render_bytes']<=640*360*12
  assert ('finish' in d['onboarding']['actions'])==(page>=3)
  for b in d['onboarding']['actions'].values():assert b[3]-b[1]+1>=44
  action('replay');capture('onboarding-'+str(page+1)+'-start');captures+=1
  wait(lambda:state()['onboarding']['phase']>=1);action('pause');before=state()['onboarding']['elapsed']
  time.sleep(.25);assert state()['onboarding']['elapsed']==before;capture('onboarding-'+str(page+1)+'-middle');captures+=1
  action('pause');wait(lambda:state()['onboarding']['phase']==3);action('pause');capture('onboarding-'+str(page+1)+'-end');captures+=1
  performance.append({key:state()['onboarding'][key] for key in ('render_frames','render_snapshots','render_mean_us','render_max_us','render_bytes')})
  if page==2:
   action('configure');wait(lambda:state()['view']=='provider');tap(40,32)
   wait(lambda:state()['view']=='onboarding');assert state()['onboarding']['step']==2
  action('next')
  if page<6:wait(lambda:state()['onboarding']['step']==page+1)
 wait(lambda:state()['view']=='chat');assert c['pid']()==native;capture('onboarding-finished')
 # Completion survives a clean native-only restart; the MiniApp host stays alive.
 c['close']();c['start_app']();wait(lambda:state()['visible']);assert state()['view']=='chat'
 c['result']['onboarding']={'pages':7,'captures':captures,'mandatory':True,'pause_replay':True,'configuration_return':True,'completed_restart':True,'synthetic_only':True,'renderer':'software_3d','performance':performance}
