"""Twenty real capture/discard cycles in the release owner's isolated fixture.

No optical-quality assertion and no confirmation/send. Hardware identity remains
owned by the production profile gate; logical geometry always comes from Falcon.
"""
import json
import pathlib
import sqlite3
import tempfile
from release_lifecycle import pull_database

PROFILES={
 'MEB0400008900794':('orange-sc2356',496,360),
 'MEA2300009007113':('y02-sc035gs',640,480),
 '7G12000010905426':('y07-sc2356',752,440),
 'JDA0700006552216':('s6-pag7920',320,240),
}

def run(d):
 profile,width,height=PROFILES[d.serial]
 d.chat()
 def ready():
  s=d.state();r=s.get('scanner',{})
  if r.get('type') in ('fatal','error'):raise AssertionError('Scanner preparation failed: '+json.dumps(r))
  return r if r.get('type')=='ready' and r.get('model_ready') else None
 first=d.wait(ready,90);worker=str(first['worker_pid'])
 assert first['profile']==profile and [first['sensor_width'],first['sensor_height']]==[width,height]
 key='/dev/input/by-path/led_control';code=134
 if d.s6:
  keys=json.loads(d.sh('cat /usr/configs/hal_key_config.json'))['Plum_V0']
  leds=json.loads(d.sh('cat /usr/configs/hal_led_config.json'))['Plum_V0']
  assert keys['head_key_id']=='platform-led_control-event' and int(keys['head_key_code'])==134
  key='/dev/input/by-path/'+keys['head_key_id'];light='/sys/class/gpio/gpio'+str(int(leds['white_gpio_number']))+'/value'
 else:
  light='/sys/class/leds/flash-led-gpio/brightness' if profile=='y07-sc2356' else '/sys/class/gpio/gpio43/value'
 def count_messages():
  with tempfile.TemporaryDirectory(prefix='penchat-scan-count-') as folder:
   path=pathlib.Path(folder)/'fixture.db';pull_database(d.serial,d.data,path,d.sh)
   with sqlite3.connect(path) as db:return db.execute('select count(*) from chat_messages').fetchone()[0]
 baseline=count_messages();results=[]
 d.capture('scanner-cycle-ready')
 for cycle in range(20):
  assert str(d.wait(ready,90)['worker_pid'])==worker
  before=d.state()['scan_id']
  receipt=d.sh(d.key_helper+' '+key+' '+str(code)+' 1 500 100')
  assert 'sent 1 paired events code=134' in receipt
  def finished():
   s=d.state();r=s.get('scan_result',{})
   return r if r.get('type')=='finished' and r.get('scanId')!=before else None
  result=d.wait(finished,120);counts=result['counts']
  assert result['profile']==profile and counts['received']>=5
  assert counts['spilled']==0 and counts['discarded_backlog']==0
  assert counts['system_reserve_bytes']>=32*1024*1024
  assert d.sh('cat '+light)=='0'
  if cycle in (0,19):d.capture('scanner-cycle-result-'+str(cycle))
  results.append({'cycle':cycle,'received':counts['received'],'selected':counts['selected'],
    'usable':result['usable'],'status':result['status'],'reason':result.get('reason','')})
  # Native edge-back discards review; never press the confirmation control.
  d.drag(12,d.h//2,min(d.w-1,160),d.h//2)
  d.wait(lambda:d.state()['view']=='chat',30)
  d.wait(ready,90)
  d.settings();d.wait(lambda:d.state()['scan_released'],30)
  assert d.sh('cat '+light)=='0' and d.sh('pidof CaptureFrame')
  assert d.sh('pidof penchat-scanner')==worker
  d.chat();d.wait(ready,90)
 assert count_messages()==baseline,'Discarded scan created a message'
 return {'profile':profile,'sensor_geometry':[width,height],'cycles':results,
   'discard_only':True,'messages_unchanged':True,'worker_retained':True,
   'system_capture_resumed':True,'paper_quality':'user reported basic operation normal; not measured by this automatic capture test'}
