"""CVI preview-only regression after restoring the original interrupted data."""
import time

def run(d):
 assert d.s6
 d.settings();d.tap(d.reveal(lambda s:s['settings']['rows']['feedback']['bounds']))
 d.wait(lambda:d.state()['view']=='feedback' and d.state()['feedback']['screen']=='form')
 d.wait(lambda:d.state()['scan_released'])
 boot=d.sh('cat /proc/sys/kernel/random/boot_id');host=d.sh('pidof miniapp');pid=d.pid()
 def opens():return int(d.sh('dmesg | grep -c cvi_isp_open || true') or '0')
 before=opens();frames=[]
 for i in range(3):
  d.tap(d.reveal(lambda s:s['feedback']['actions']['preview']))
  d.wait(lambda:d.state()['feedback']['screen']=='preview',30)
  time.sleep(.3);assert d.pid()==pid and d.sh('cat /proc/sys/kernel/random/boot_id')==boot
  d.capture('feedback-metadata-preview-'+str(i));frames.append(i)
  assert opens()==before,'Unexpected ISP open while preparing metadata preview'
  assert not d.sh('find '+d.data+'/.feedback/reports -type f 2>/dev/null || true')
  if i<2:
   d.touch(40,32);d.wait(lambda:d.state()['feedback']['screen']=='form')
 assert d.sh('pidof miniapp')==host
 return {'preview_cycles':len(frames),'isp_opens_before':before,'isp_opens_after':opens(),'same_native_pid':True,'same_host_pid':True,'same_boot_id':True,'uploaded_reports':0}
