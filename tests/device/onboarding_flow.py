"""Native onboarding using physical touchscreen events in isolated user directories."""
import time

def run(c):
 state,tap,wait,capture=c['state'],c['tap'],c['wait'],c['capture']
 def action(key):
  before=state();b=before['onboarding']['actions'][key];tap((b[0]+b[2])//2,(b[1]+b[3])//2)
  if key=='pause':
   # On the software-rendered S6 scene, the diagnostic tick can precede
   # handling the helper's release frame. Observe the toggle before timing it.
   wait(lambda:state()['onboarding']['paused'] != before['onboarding']['paused'])
  wait(lambda:not (state().get('onboarding') or {}).get('busy',False))
 assert state()['view']=='onboarding'
 native=c['pid']();captures=0;performance=[]
 for page in range(7):
  d=state();assert d['onboarding']['step']==page and not d['editor']['active'] and not d['keyboard']
  assert d['onboarding']['renderer']=='software_3d' and not d['onboarding']['render_failed']
  assert 0<d['onboarding']['render_bytes']<=640*360*12
  assert ('finish' in d['onboarding']['actions'])==(page>=3)
  for b in d['onboarding']['actions'].values():assert b[3]-b[1]+1>=44
  controls=list(d['onboarding']['actions'].values())
  for i,b in enumerate(controls):
   for other in controls[i+1:]:
    assert b[2]<other[0] or other[2]<b[0] or b[3]<other[1] or other[3]<b[1],('guide controls overlap',page,b,other)
  if page==4 and d['onboarding'].get('reader_scroll_max',0)>0:
   b=d['onboarding']['reader_bounds'];x=(b[0]+b[2])//2
   c['drag'](x,b[3]-5,x,b[1]+5)
   wait(lambda:state()['onboarding']['reader_scroll']>0)
   capture('onboarding-description-scrolled')
  action('replay');capture('onboarding-'+str(page+1)+'-start');captures+=1
  wait(lambda:state()['onboarding']['phase']>=1);action('pause');before=state()['onboarding']['elapsed']
  time.sleep(.25);assert state()['onboarding']['elapsed']==before;capture('onboarding-'+str(page+1)+'-middle');captures+=1
  action('pause');wait(lambda:state()['onboarding']['phase']==3);action('pause');capture('onboarding-'+str(page+1)+'-end');captures+=1
  performance.append({key:state()['onboarding'][key] for key in ('render_frames','render_snapshots','render_mean_us','render_max_us','render_bytes')})
  if page==2:
   action('configure');wait(lambda:state()['view']=='provider');c['navigate_back_or_menu']()
   wait(lambda:state()['view']=='onboarding');assert state()['onboarding']['step']==2
  action('next')
  if page<6:wait(lambda:state()['onboarding']['step']==page+1)
 wait(lambda:state()['view']=='chat');assert c['pid']()==native;capture('onboarding-finished')
 # Completion survives a clean native-only restart; the MiniApp host stays alive.
 c['close']();c['start_app']();wait(lambda:state()['visible']);assert state()['view']=='chat'
 c['result']['onboarding']={'pages':7,'captures':captures,'mandatory':True,'pause_replay':True,'configuration_return':True,'completed_restart':True,'synthetic_only':True,'renderer':'software_3d','performance':performance}
